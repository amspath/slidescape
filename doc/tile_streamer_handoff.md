# Tile Cache Handoff

This document records the current state and next direction for tile scheduling,
tile cache ownership, stale viewer work, and future CPU/GPU eviction.

The previous standalone `tile_streamer` experiment from commit `08601b72`
improved Slide Score and other latency-bound viewer loading by bounding in-flight
work and discarding stale viewer-only requests. That optimization was removed in
the restart because it was layered on top of old ownership: `tile_t` flags,
caller-owned completion queues, and ad hoc CPU retention. The optimization is
still desirable, but it should now be rebuilt inside the shared tile cache model.

## Current State

Generic non-iSyntax tile loading has moved substantially toward shared cache
ownership:

- `image_t` owns an optional `tile_cache_t`.
- `tile_t` is now mostly geometry and draw bookkeeping: `tile_index`, `tile_x`,
  `tile_y`, `is_empty`, and `time_last_drawn`.
- `tile_cache_t` owns per-tile CPU pixels, GPU texture handles, decode/upload
  busy state, demand bits, pin counts, access timestamps, and a per-image result
  queue.
- Generic tile workers post decoded results to the cache result queue through
  `tile_cache_post_load_result()`.
- The viewer drains cache result queues and stores uploaded textures back into
  the cache.
- `image_read_region()` pins needed CPU tiles, joins in-flight viewer work,
  waits on shared cache state, copies from cache-owned pixels, and unpins after
  copying.
- Viewer cache-result draining is frame-budgeted using the same texture upload
  pacing logic that previously applied to the global completion queue.

This means the central invariant is now mostly true for generic loading:
workers produce decoded tile results into a per-image cache, and consumers
observe cache state instead of owning private completion queues.

## Still Not Done

The current code is still a migration midpoint, not the final design.

- `request_tiles()` still exists as a public scheduling API.
- Viewer still builds a per-frame wishlist directly in `viewer.cpp`.
- `load_tile_task_t` still contains compatibility fields such as `tile_t*`,
  `tile_x`, `tile_y`, `need_gpu_residency`, `need_cpu_residency`, `task_group`,
  and refcount plumbing.
- Cached CPU-to-GPU upload still uses the global
  `TILE_LOADER_COMPLETION_EVENT_UPLOAD_CACHED_TILE` path.
- Cache result entries still reuse `TILE_LOADER_COMPLETION_EVENT_TILE_LOADED`
  instead of a cache-native result type.
- iSyntax still uses its private streamer and the global completion queue. This
  is intentionally separate for now.
- There is no memory-budgeted eviction yet.
- Stale viewer cancellation/discard has not been reintroduced yet.

## Actual Problem

Tile loading needs to separate these concerns:

- demand: who wants a tile and why;
- scheduling: what work should be submitted next;
- I/O/decode: how a backend obtains pixels;
- completion delivery: how decoded results enter shared state;
- CPU residency: whether decoded pixels are retained in RAM;
- GPU residency: whether a renderer texture exists;
- stale viewer work: whether work is still useful for the current camera;
- eviction: which resident resources can be released under pressure.

The old code mixed these in `tile_t` and in caller-owned completion queues. The
restart has moved CPU/GPU residency and request busy state into `tile_cache_t`.
The next step is to move viewer scheduling and stale-work policy there too.

## Target Invariant

The target invariant remains:

- The cache owns per-tile request state and residency.
- Workers post decoded tile results into a long-lived per-image cache queue.
- Consumers express demand and wait on cache state.
- Viewer demand is soft and generation-based.
- `image_read_region()`, export, and registration demand is hard and must not be
  cancelled or evicted while pinned.
- GPU upload is a cache/viewer operation driven by cache state, not by a global
  completion event.

## Current Cache Shape

The current cache state is close to the intended shape:

```c
typedef struct tile_cache_tile_t {
    u8 request_state;
    u32 demand_mask;
    u32 cpu_pin_count;
    u32 gpu_pin_count;
    bool8 cpu_resident;
    bool8 gpu_resident;
    bool8 decode_in_flight;
    bool8 upload_pending;
    bool8 is_empty;
    u8* pixels;
    renderer_texture_handle_t texture;
    i32 generation;
    i32 priority;
    i64 last_cpu_access_time;
    i64 last_gpu_access_time;
} tile_cache_tile_t;
```

Fields that still need stronger semantics:

- `generation`: should become the viewer/camera generation used to identify
  stale soft demand.
- `priority`: should represent the best current scheduling priority across all
  active demand.
- `demand_mask`: should distinguish soft viewer demand from hard CPU/export
  demand and determine whether stale discard is allowed.
- `gpu_pin_count`: exists but is not yet meaningfully used.
- `is_empty`: exists in cache state but generic code still primarily uses
  `tile_t::is_empty`.

## Reintroducing The Slide Score / IO-Bound Optimization

Commit `08601b72` introduced a useful optimization: for latency-bound viewer
loads, do not let stale camera requests monopolize workers or HTTP requests. It
used a standalone `tile_streamer_t` with:

- camera generations;
- per-backend scheduling policy;
- max in-flight tile limits;
- max submissions per tick;
- small batch size for Slide Score;
- stale checks before expensive read/decode/upload stages;
- protection for hard/pinned demand.

The right path is to reintroduce those ideas as cache scheduling policy, not as a
separate tile streamer owner.

Recommended cache-side API:

```c
typedef struct tile_cache_viewer_request_t {
    bounds2f camera_bounds;
    bounds2f crop_bounds;
    v2f camera_center;
    i32 zoom_level;
    i32 client_width;
    i32 client_height;
    bool8 is_cropped;
} tile_cache_viewer_request_t;

void tile_cache_request_viewer_tiles(image_t* image,
                                     tile_cache_viewer_request_t* request);
```

The implementation should:

- update `cache->viewer_generation` when the camera/crop/zoom changes;
- compute visible tiles and priorities currently computed in `viewer.cpp`;
- mark visible tiles with soft `TILE_CACHE_DEMAND_VIEWER_VISIBLE` demand;
- submit only up to a backend policy limit;
- bound in-flight viewer-only work;
- assign each viewer task the current generation;
- allow viewer-only stale tasks to be discarded before slow/expensive stages;
- refuse stale discard when hard CPU/export/registration demand or pins exist.

Backend policy should live near the cache scheduler, for example:

```c
typedef struct tile_cache_policy_t {
    i32 max_inflight_tiles;
    i32 max_submit_per_tick;
    i32 batch_size;
    bool8 discard_stale_before_read;
    bool8 discard_stale_before_decode;
    bool8 discard_stale_before_upload;
    bool8 latency_bound;
} tile_cache_policy_t;
```

Initial policies can mirror the useful parts of `08601b72`:

- Slide Score: latency-bound, small batch size, higher in-flight cap than local
  disk, stale discard enabled.
- Remote TIFF: latency-bound, low submit rate, stale discard enabled.
- OpenSlide/local slow storage: lower in-flight cap, stale discard enabled before
  decode/upload where safe.
- Local TIFF/DICOM/MRXS: moderate submit rate; stale discard still useful but
  less critical than for HTTP-bound backends.

The important rule is that stale discard is allowed only for viewer-only soft
work. If a tile has CPU pins or hard demand, stale checks must return false.

Suggested worker-side check:

```c
bool tile_cache_task_is_stale(image_t* image,
                              i32 level,
                              i32 tile_index,
                              i32 generation);
```

This should consult cache state, not recompute ownership through `tile_t`.

## CPU/GPU Eviction Readiness

We are partially capable of implementing eviction now, but not all the way to a
robust memory-pressure cache yet.

What is already available:

- CPU pixels are cache-owned.
- GPU textures are cache-owned.
- CPU pin counts exist and are used by `image_read_region()`.
- Access timestamps exist for CPU and GPU residency.
- Demand flags exist.
- CPU pixels can already be released when unpinned via
  `tile_cache_release_cpu_pixels_if_unpinned()`.
- GPU textures can be removed from cache ownership with
  `tile_cache_take_gpu_texture()` and destroyed by the renderer owner.

What is missing for real eviction:

- explicit CPU and GPU byte accounting;
- configurable memory budgets;
- a cache eviction pass that scans/scores candidates;
- renderer-safe GPU texture destruction from the eviction path;
- reliable GPU pin semantics;
- clear rules for keeping CPU pixels after GPU upload;
- tests proving pinned tiles are never evicted;
- tests proving unpinned resident tiles can be evicted and later reloaded.

So yes: the current architecture is now capable of supporting eviction in a way
the old design was not. But the implementation should be added deliberately, not
as scattered opportunistic frees.

Recommended eviction API:

```c
typedef struct tile_cache_budget_t {
    u64 max_cpu_bytes;
    u64 max_gpu_bytes;
} tile_cache_budget_t;

void tile_cache_set_budget(image_t* image, tile_cache_budget_t budget);
void tile_cache_note_memory_pressure(image_t* image);
void tile_cache_evict_to_budget(image_t* image, app_state_t* app_state);
```

Eviction rules:

- Never evict CPU pixels with `cpu_pin_count > 0`.
- Never evict GPU textures with `gpu_pin_count > 0`.
- Do not evict tiles with decode/upload in flight unless the state machine
  explicitly supports cancellation.
- Prefer evicting stale viewer-generation tiles first.
- Prefer evicting tiles far from the current view.
- Prefer evicting old fine-detail tiles before overview tiles.
- CPU and GPU eviction should be independent: it is valid to drop GPU texture
  residency while keeping CPU pixels.

A minimal first eviction pass can be LRU-like:

- track CPU/GPU byte counts;
- scan all unpinned resident tiles;
- evict oldest `last_cpu_access_time` CPU pixels until under budget;
- evict oldest `last_gpu_access_time` GPU textures until under budget.

A later pass can add view distance, generation, level bias, and backend-specific
costs.

## Recommended Remaining Plan

1. Update the cache result queue to use a cache-native result type instead of
   tile-loader completion event kinds.
2. Remove the generic global `UPLOAD_CACHED_TILE` event by making cached
   CPU-to-GPU upload a cache/viewer path.
3. Add `tile_cache_request_viewer_tiles()` and move viewer wishlist construction
   out of `viewer.cpp`.
4. Add cache policy state: viewer generation, backend policy, in-flight counts,
   submit-per-frame limits, and stale-discard flags.
5. Reintroduce stale viewer discard for soft demand only, using cache demand and
   pin state to protect hard demand.
6. Simplify `load_tile_task_t` so worker tasks describe backend decode work:
   image, level, tile index, priority, generation, invert colors, and requested
   residency/demand metadata.
7. Add deterministic cache tests for request merging, pins, stale discard, and
   cache result draining.
8. Add CPU/GPU byte accounting and a conservative LRU eviction pass.
9. Add policy-based eviction scoring once the simple eviction path is tested.
10. Decide later whether iSyntax should remain separate or be adapted to the
    same cache scheduler. Do not force this prematurely.

## Testing Priorities

Start with deterministic cache-state tests before GUI behavior tests:

- viewer request schedules a missing tile;
- repeated viewer request does not duplicate in-flight work;
- viewer camera generation change makes old viewer-only work stale;
- hard CPU demand joins an in-flight viewer decode;
- hard CPU demand prevents stale discard;
- `image_read_region()` waits on cache state, not a private worker queue;
- overlapping CPU pins keep pixels alive until the last unpin;
- cache-drained viewer results are retained when a CPU pin exists;
- unpinned CPU resident tiles can be evicted;
- pinned CPU resident tiles are not evicted;
- unpinned GPU resident tiles can be evicted through renderer-safe destruction;
- pinned GPU resident tiles are not evicted.

The first integration test should exercise `image_read_region()` reading a tile
that was already requested by the viewer. That is the case the old design could
not make safe without queue ownership hacks.

## Current Direction

Proceed cache-first. Do not resurrect the old standalone `tile_streamer` files.
The old streamer proved that bounded, generation-aware, stale-discard scheduling
improves Slide Score and latency-bound I/O. The restart has built the ownership
model that can make that optimization safe. The next work should put streamer
policy inside `tile_cache`, where it can respect hard CPU demand, pins, and
shared residency.
