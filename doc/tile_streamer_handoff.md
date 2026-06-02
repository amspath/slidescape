# Tile Streamer And Cache Handoff

This note captures the current thinking around viewer tile scheduling, stale tile work, `image_read_region()`, and future CPU/GPU tile cache eviction. It is intended as a handoff document for continuing the tile streamer work after the first generic scheduler pass.

## Current State

The viewer now routes non-iSyntax WSI tile requests through `src/core/tile_streamer.c`. The first pass deliberately kept the change narrow:

- `viewer.cpp` asks `tile_streamer_request_tiles()` for visible tile work instead of building the old per-frame wishlist inline.
- `tile_streamer.c` computes visible tiles, priorities, a camera generation, and a bounded inflight window.
- `load_tile_task_t` carries `stream_generation` and `may_discard_if_stale`.
- `tile_load_completion_task_t` carries `stream_generation` and `stale`.
- Local tile loading, remote TIFF loading, and Slide Score loading call `tile_streamer_is_task_stale()` to skip or discard work that is no longer useful for the current camera.
- Slide Score now submits one tile per work item instead of serial batches of up to eight tiles, which improves responsiveness under latency-bound HTTP I/O.

This has improved Slide Score responsiveness, but the streamer is not yet a full owner of tile state. Most persistent scheduling state still lives directly in `image_t` and `tile_t`:

- `image_t::tile_stream_generation`
- `image_t::tile_stream_last_camera_*`
- `tile_t::is_submitted_for_loading`
- `tile_t::submitted_stream_generation`
- `tile_t::need_keep_in_cache`
- `tile_t::read_region_refcount`

This was acceptable for a first pass, but it is not the desired long-term shape.

## Design Goals

The next design should support these goals:

1. Viewer panning should not let old camera requests monopolize worker threads, remote HTTP connections, decode time, texture upload bandwidth, or cache space.
2. Local backends should also benefit from scheduling, especially on HDDs and network drives where latency and seek order matter.
3. `image_read_region()` should share the same CPU tile cache and should not reload tiles that are already cached.
4. `image_read_region()` and export work must be able to protect the tiles they need from camera-driven cancellation and cache eviction.
5. CPU tile memory and GPU tile residency should eventually be evicted by explicit priority, not by ad hoc flags.
6. Backend policies should be explicit and measurable.
7. Instrumentation should make scheduling and cache behavior observable enough to tune.

## Important Conceptual Split

Separate these concerns:

- **Demand:** who currently wants a tile and why.
- **Scheduling:** what work should be submitted next.
- **I/O/decode:** how a backend reads and decodes tile pixels.
- **CPU residency:** whether decoded pixels are cached in RAM.
- **GPU residency:** whether the tile has a renderer texture.
- **Eviction:** which CPU/GPU resident tiles can be discarded under pressure.

The current code mixes several of these in `tile_t`. For example, `is_submitted_for_loading` is used as a scheduler state, a duplicate-submission guard, and a rough lock shared between viewer and `image_read_region()`. `need_keep_in_cache` is used both as a request flag and as a cache retention hint. This makes stale work and eviction hard to reason about.

## Recommended Ownership Model

Introduce a real `tile_streamer_t` instead of storing streamer fields directly in `image_t`.

Proposed rough shape:

```c
typedef struct tile_streamer_t {
	image_t* image;
	i32 generation;
	tile_streamer_policy_t policy;
	tile_streamer_stats_t stats;
	tile_stream_tile_state_t* level_states[IMAGE_PYRAMID_MAX_LEVELS];
} tile_streamer_t;
```

`image_t` should eventually keep only a pointer or embedded streamer:

```c
tile_streamer_t* tile_streamer;
```

Embedding avoids one allocation but grows `image_t`. A pointer keeps `image_t` cleaner and allows the streamer to be optional for headless/read-only use. Either is acceptable; prefer the option that fits surrounding allocation/lifetime code with less churn.

## Per-Tile State

Add explicit streamer/cache state per tile, preferably outside `tile_t` at first:

```c
typedef enum tile_stream_request_state_enum {
	TILE_STREAM_UNREQUESTED,
	TILE_STREAM_QUEUED,
	TILE_STREAM_IN_FLIGHT,
	TILE_STREAM_READY,
	TILE_STREAM_FAILED,
} tile_stream_request_state_enum;

typedef struct tile_stream_tile_state_t {
	u8 request_state;
	i32 generation;
	i32 priority;
	i32 last_wanted_generation;
	i64 last_requested_time;
	i64 last_completed_time;
	u32 active_pin_count;
	u32 demand_mask;
	bool8 stale_ok_to_cancel;
} tile_stream_tile_state_t;
```

This state should eventually replace most viewer-path reliance on `tile->is_submitted_for_loading`. Keep `tile->is_submitted_for_loading` temporarily as a compatibility field for direct read paths while migrating.

Do not treat `READY` as equivalent to GPU-ready. It should mean the scheduled read/decode request completed. CPU/GPU residency should be tracked separately.

## Demand Classes

The streamer should distinguish why a tile is wanted:

```c
typedef enum tile_demand_flags_enum {
	TILE_DEMAND_VIEWER_VISIBLE = 1 << 0,
	TILE_DEMAND_VIEWER_PREFETCH = 1 << 1,
	TILE_DEMAND_READ_REGION = 1 << 2,
	TILE_DEMAND_EXPORT = 1 << 3,
	TILE_DEMAND_REGISTRATION = 1 << 4,
} tile_demand_flags_enum;
```

Viewer demand is soft and can become stale when the camera moves. `image_read_region()` and export demand are hard: they must complete and must not be dropped just because the viewer moved.

This implies:

- Viewer-only queued work can be cancelled or deprioritized.
- Viewer-only in-flight work can be discarded before decode/upload if it is stale.
- Read-region/export in-flight work must not be cancelled.
- A tile wanted by both viewer and read-region must inherit the stricter behavior while the hard demand exists.

## Pinning And Retention

Replace the current ad hoc interaction between `read_region_refcount`, `need_keep_in_cache`, and cache release with a clearer pinning model.

Suggested distinction:

- `active_pin_count`: short-lived guarantee that tile pixels/textures must not be evicted.
- `cpu_cache_refcount` or `cpu_pin_count`: protects decoded CPU pixels.
- `gpu_pin_count`: protects renderer texture residency, if needed.
- `read_region_refcount`: can either become a specific pin reason or remain as a compatibility field while migrating.

`image_read_region()` should acquire CPU pins for every source tile before it waits or copies. It should release those pins after copying. While a CPU pin exists:

- the tile must not be evicted from CPU cache;
- if a tile load is in flight, the load must keep CPU pixels on completion;
- viewer staleness must not discard the result before CPU pixels are cached.

This can be represented either as a pin count in streamer/cache state or as demand flags plus pin count. Avoid a single boolean such as `need_keep_in_cache`, because multiple consumers can overlap.

## `image_read_region()` Interaction

`image_read_region()` has special requirements:

1. It already skips tiles that are `tile->is_cached && tile->pixels`.
2. It increments `tile->read_region_refcount` and sets `tile->need_keep_in_cache`.
3. It waits until all required tiles are cached or empty.
4. It can overlap with other `image_read_region()` calls and with viewer tile requests.
5. It owns a local completion queue that must outlive every task it submits.

The future design should keep those safety properties while sharing the streamer/cache state.

Recommended direction:

- Keep `image_read_region()` synchronous from the caller's point of view.
- Let it register hard demand for a tile range through a shared tile request/cache API.
- If a required tile is already CPU cached, just pin and use it.
- If a required tile is viewer-in-flight, upgrade that in-flight request with hard demand:
  - mark it non-cancellable;
  - force `need_keep_in_cache`/CPU-retain behavior;
  - optionally add a completion notification for the read-region waiter, or let the waiter poll shared state until the tile is cached.
- If a required tile is queued but not in flight, raise priority and mark hard demand.
- If a required tile is not requested, submit a hard-demand request.

The important part is that a viewer request and a read-region request for the same tile should converge into one tile load, not two. The resulting pixels should be retained in CPU cache if any hard demand needs them.

### Completion Queue Problem

Today each submitted task has exactly one completion queue. That is awkward when a viewer request is later upgraded by `image_read_region()`, because the in-flight worker will post only to the original queue.

Possible solutions:

1. **Shared state polling:** workers update tile/cache state, and `image_read_region()` waits on shared state rather than requiring its own completion event for every tile. This is simple but may need careful sleeping/wakeup behavior.
2. **Multiple waiters:** tile state keeps a small list or count of waiters. Completion posts to all registered queues. This is more complex and must avoid lifetime bugs.
3. **Central completion queue:** all tile completions go to a long-lived central queue; viewer and read-region both process or wait on shared state. This avoids short-lived queue lifetime problems but requires a stronger central tile-cache state machine.

Prefer option 1 or 3. Avoid attaching short-lived completion queues to requests that may be merged with older viewer work unless the lifetime rules are very explicit.

### Request Upgrades

Define explicit upgrade behavior:

```c
typedef struct tile_request_options_t {
	u32 demand_flags;
	bool need_cpu_residency;
	bool need_gpu_residency;
	bool can_discard_if_stale;
	i32 priority;
} tile_request_options_t;
```

If a second request arrives for a tile already queued/in-flight:

- OR the demand flags.
- Raise priority to the maximum.
- Set `need_cpu_residency` if any requester needs it.
- Set `need_gpu_residency` if any requester needs it.
- Set `can_discard_if_stale = false` if any hard requester needs completion.

This turns stale checking into a cheap state lookup instead of recomputing current visibility repeatedly.

## Cache State

Add cache metadata separately from request state:

```c
typedef struct tile_cache_state_t {
	bool8 cpu_resident;
	bool8 gpu_resident;
	bool8 cpu_evictable;
	bool8 gpu_evictable;
	u32 cpu_pin_count;
	u32 gpu_pin_count;
	u32 cpu_bytes;
	i64 last_drawn_time;
	i64 last_cpu_access_time;
	i64 last_gpu_access_time;
	i32 cache_priority;
} tile_cache_state_t;
```

Some fields already exist in `tile_t` (`pixels`, `texture`, `is_cached`, `time_last_drawn`). They can stay there initially, but eviction decisions should use a coherent cache state API instead of directly checking scattered fields.

## Eviction Direction

Eviction should be priority based and should never evict pinned resources.

Possible CPU eviction score:

```text
score =
	age_since_last_access
	+ distance_from_current_view_weight
	+ stale_generation_weight
	+ low_zoom_detail_weight
	- high_pyramid_level_keep_bonus
	- recently_drawn_bonus
```

Interpretation:

- Far from current view is easier to evict.
- Old generation is easier to evict.
- Fine-detail low-level tiles are easier to evict than high-level overview tiles, because overview tiles are reused often and cheap to keep.
- Recently drawn or recently read tiles are harder to evict.
- Pinned tiles are not evictable.

GPU eviction should be separate from CPU eviction. It is valid to evict GPU texture residency while keeping CPU pixels, especially when VRAM pressure is high but RAM is available. GPU eviction must respect renderer lifetime/finalization rules.

## Backend Policies

Make policies explicit:

```c
typedef struct tile_streamer_policy_t {
	i32 max_inflight_tiles;
	i32 max_submit_per_tick;
	i32 batch_size;
	bool8 discard_stale_before_read;
	bool8 discard_stale_before_decode;
	bool8 discard_stale_before_upload;
	bool8 sort_by_file_offset;
	bool8 latency_bound;
} tile_streamer_policy_t;
```

Suggested initial policies:

- Slide Score:
  - high inflight count;
  - one tile per work item or very small batches;
  - discard stale before request/decode/upload;
  - no offset sorting.
- Remote TIFF:
  - moderate inflight count;
  - retain batch range reads;
  - stale discard before range request where possible;
  - consider offset grouping.
- Local TIFF on SSD:
  - moderate inflight count;
  - visual priority first;
  - offset sorting only within a small top-priority window.
- Local TIFF on HDD/network drive:
  - lower inflight count for HDD;
  - sort top-priority candidates by file offset to reduce seeks;
  - use larger sequential batches if offsets are close.
- OpenSlide:
  - conservative inflight count, because backend behavior is opaque and may already cache internally.
- DICOM/MRXS:
  - start conservative; tune after instrumentation.

## Offset Ordering

Offset ordering should not override visual priority globally. A good compromise:

1. Build a visually prioritized candidate list.
2. Keep only the top N candidates for this tick.
3. For backends with known offsets, sort that subset by file offset or by offset cluster.

This preserves responsiveness while improving HDD/network-drive throughput.

For TIFF this requires exposing tile byte offsets through a backend helper. Avoid making generic streamer code know TIFF internals directly if possible.

## Instrumentation

Add stats early, before more tuning:

```c
typedef struct tile_streamer_stats_t {
	u64 submitted;
	u64 completed;
	u64 failed;
	u64 stale_before_read;
	u64 stale_after_read;
	u64 stale_before_upload;
	u64 merged_requests;
	u64 upgraded_requests;
	i32 queued_count;
	i32 inflight_count;
	i64 cpu_cached_bytes;
	i32 gpu_resident_tiles;
	double read_ms_sum;
	double decode_ms_sum;
	double upload_ms_sum;
} tile_streamer_stats_t;
```

Expose this in a debug ImGui tree or console logging toggle. The first useful questions are:

- How many viewer tasks are discarded as stale?
- How often does `image_read_region()` reuse CPU cached tiles?
- How often does `image_read_region()` wait on viewer-in-flight tiles?
- What is the average read/decode/upload time per backend?
- What is the current queue and inflight count?
- How many CPU/GPU resident tiles exist per level?

## Testing Strategy

Targeted tests can cover rare scheduler/cache corner cases much better than manual viewer use. The main requirement is to make the scheduler and cache state machine testable without real WSI files, HTTP, OpenGL, worker timing, or fixture data.

Prefer direct state-machine tests first. Add integration tests only after the state transitions are deterministic and isolated enough to exercise without sleeps or real I/O.

### Testable Core

Extract or expose a small scheduler/cache state API that can be driven synchronously by tests:

```c
tile_streamer_request_viewer_tiles(...);
tile_streamer_request_region_tiles(...);
tile_streamer_mark_task_started(...);
tile_streamer_complete_task(...);
tile_streamer_fail_task(...);
tile_cache_pin_cpu_tile(...);
tile_cache_unpin_cpu_tile(...);
tile_cache_try_evict(...);
```

These functions do not need to be the exact public API names, but tests should be able to simulate request, upgrade, completion, failure, and eviction transitions without launching worker threads.

Use a fake tile backend for tests:

```c
typedef struct fake_tile_backend_t {
	i32 submitted_count;
	i32 completed_count;
	i32 cancelled_before_read_count;
	i32 decoded_count;
	i32 uploaded_count;
} fake_tile_backend_t;
```

The fake backend should record what would have happened, not perform actual reads or decodes.

### Unit Test Cases

Useful state-machine tests:

- Viewer-visible request enters `QUEUED`, then `IN_FLIGHT`, then `READY`.
- Viewer request becomes stale when the camera generation changes and the tile is no longer visible.
- Viewer request remains valid across a camera generation change if the tile is still visible.
- Viewer-only queued work can be cancelled or deprioritized after camera movement.
- Viewer-only in-flight work can be discarded before decode or upload when stale.
- `image_read_region()` hard demand upgrades a viewer-queued request to non-cancellable.
- `image_read_region()` hard demand upgrades a viewer-in-flight request so completion keeps CPU pixels.
- A viewer-stale in-flight request is not discarded if read-region/export demand was added before completion.
- A cached CPU tile is reused by read-region demand without submitting another load.
- Multiple read-region demands pin the same CPU tile and release it safely.
- A CPU-pinned tile is never selected for CPU eviction.
- A GPU-pinned or upload-pending tile is never selected for GPU eviction.
- Failed tile state does not cause infinite resubmission unless retry policy explicitly allows it.
- Backend policy changes submission shape, e.g. Slide Score submits single-tile high-inflight work while TIFF keeps batched or offset-ordered work.
- Offset ordering sorts only the top-priority submission window, not the entire visible region.

### `image_read_region()` Integration Cases

After the shared tile state exists, add focused integration tests around `image_read_region()` using a synthetic/fake image backend:

- Region read hits an already CPU-cached tile and submits no work.
- Region read overlaps a viewer-in-flight tile; the request is upgraded, not duplicated.
- Viewer camera moves away while region read is waiting; the shared tile request still completes and CPU pixels are retained.
- Two concurrent or interleaved region reads overlap the same tile; refcounts/pins keep pixels alive until both have copied.
- Region read receives a tile that was originally requested for GPU-only viewer display; completion still retains CPU pixels because the request was upgraded.

Avoid real sleeps in these tests where possible. If a wait loop must be tested, use a fake queue or explicit step function so the test controls exactly when work starts and completes.

### Eviction Tests

Eviction should be tested independently from real memory pressure:

- Given a set of resident tiles with different priorities, the lowest-priority evictable tile is chosen.
- Pinned tiles are skipped even if they have the worst score.
- High-level overview tiles receive the intended keep bonus relative to fine-detail tiles.
- Recently drawn/read tiles are harder to evict than stale tiles.
- CPU eviction can happen without GPU eviction, and GPU eviction can happen without CPU eviction, subject to pins.

The goal is not to perfectly predict every eviction decision, but to lock down the invariants that prevent correctness bugs.

### Instrumentation Tests

Where stats are added, include lightweight tests that counters move in the expected direction:

- submitting work increments submitted/queued/inflight counts;
- stale discard increments the appropriate stale counter;
- request merge increments `merged_requests`;
- request upgrade increments `upgraded_requests`;
- completion decrements inflight and increments completed or failed.

Instrumentation tests should avoid asserting exact timing values. Test that timing counters are recorded or non-negative, not precise durations.

### What Not To Test First

Do not start with full GUI/OpenGL tests for this work. They are useful later for smoke coverage, but they are too indirect for scheduler correctness. Also avoid tests that require real Slide Score servers or private WSI fixtures. The rare bugs here are mostly state-transition and lifetime bugs, so deterministic fake-backend tests will provide better coverage.

## Migration Plan

Recommended next implementation steps:

1. Introduce `tile_streamer_t` and move the generation/camera fields out of `image_t`.
2. Allocate per-level `tile_stream_tile_state_t` arrays when initializing WSI levels.
3. Use explicit request states for the viewer path:
   - `UNREQUESTED`
   - `QUEUED`
   - `IN_FLIGHT`
   - `READY`
   - `FAILED`
4. Replace repeated `tile_streamer_is_task_stale()` recomputation with state-driven stale flags where practical.
5. Add demand flags and request upgrade behavior.
6. Let `image_read_region()` register hard CPU demand through the same request/cache API, but keep its synchronous wait semantics.
7. Add basic stats counters.
8. Add backend policy structs.
9. Add CPU cache eviction.
10. Add GPU texture eviction.
11. Add offset ordering for TIFF-like backends.

Keep the migration incremental. Do not attempt to rewrite `image_read_region()`, viewer scheduling, CPU cache eviction, and GPU eviction in one patch.

## Caution Points

- Do not let a worker post into a completion queue that may already have been destroyed. This is the main reason request merging with `image_read_region()` needs careful design.
- Do not discard viewer-stale work if a hard demand has upgraded the same tile.
- Do not evict CPU pixels while `image_read_region()` is copying from them.
- Do not evict GPU textures while renderer upload/finalization still references them.
- Avoid adding backend-specific TIFF details directly to the generic streamer unless hidden behind a small backend policy/helper API.
- Preserve the direct `image_read_region()` behavior until the shared tile state is stable.
- iSyntax remains special because its tile loading has format-specific dependency reconstruction. It can share cache/instrumentation concepts later, but should not be forced into the generic request scheduler prematurely.

## Current Useful Files

- `src/core/tile_streamer.c`: first-pass generic viewer scheduler and stale checks.
- `src/core/tile_streamer.h`: current streamer request API.
- `src/core/tile_loader.c`: local backend tile task submission and stale completion metadata.
- `src/core/viewer_io_file.cpp`: Slide Score tile loading.
- `src/core/viewer_io_remote.c`: remote TIFF batch loading.
- `src/core/viewer.cpp`: viewer completion handling and streamer request call.
- `src/core/image.c`: `image_read_region()` and current CPU cache/read-region refcount behavior.
- `src/core/image.h`: current `tile_t` and `image_t` fields that should be migrated into streamer/cache state over time.
