# Tile Cache Restart Handoff

This document records the restart point for tile scheduling/cache work. The
previous tile-streamer direction improved stale viewer request handling, but it
kept too much old ownership in place and made `image_read_region()` interactions
harder to reason about. From this point forward this work is experimental: it is
acceptable to remove old behavior first, then restore functionality around a
cleaner shared model.

## Actual Problem

Tile loading currently conflates several independent concerns:

- request scheduling;
- completion delivery;
- CPU pixel ownership;
- GPU texture ownership;
- viewer stale cancellation;
- read-region/export/registration retention.

Those concerns are currently represented by scattered `tile_t` fields and
caller-owned completion queues. The worst examples are:

- `tile->is_submitted_for_loading`: means worker active, duplicate-submission
  guard, GPU upload pending, and "some caller owns completion".
- `tile->need_keep_in_cache`: a single boolean trying to represent multiple
  overlapping CPU consumers.
- `tile->need_gpu_residency`: a request flag stored on the tile instead of on
  demand/cache state.
- `tile->read_region_refcount`: a read-region-specific cache pin embedded in
  generic tile geometry.
- `load_tile_task_t::completion_queue`: makes the first requester own
  completion, which breaks down when viewer and `image_read_region()` need the
  same tile.

The real goal is to make tile request state and tile residency shared, explicit,
and independent from whichever subsystem first asked for a tile.

## Target Invariant

Workers produce decoded tile results into a shared per-image tile cache.
Consumers observe and wait on cache state, not on private completion queues.

The viewer is a consumer:

- It declares soft GPU demand for visible tiles in the current camera
  generation.
- Stale viewer-only work may be cancelled or ignored.
- It uploads textures from cache-owned CPU pixels when GPU residency is needed.

`image_read_region()` is a consumer:

- It declares hard CPU demand for a tile range.
- It pins those CPU tiles while waiting and copying.
- It waits until shared cache state says every source tile is CPU-ready or empty.
- It releases pins after copying.

Export and registration should use the same hard CPU-demand path as
`image_read_region()`.

## Long-Term Ownership Model

Each `image_t` owns one optional cache/request manager:

```c
typedef struct tile_cache_t tile_cache_t;

struct image_t {
    ...
    tile_cache_t* tile_cache;
};
```

The cache owns per-level, per-tile request and residency state:

```c
typedef enum tile_cache_request_state_enum {
    TILE_CACHE_UNREQUESTED,
    TILE_CACHE_QUEUED,
    TILE_CACHE_IN_FLIGHT,
    TILE_CACHE_READY,
    TILE_CACHE_FAILED,
} tile_cache_request_state_enum;

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

The geometric fields can remain in `tile_t` initially:

- `tile_index`;
- `tile_x`;
- `tile_y`;
- possibly `time_last_drawn` until rendering state moves fully into the cache.

Everything else should move out of `tile_t`.

## Cruft To Remove Early

Delete or stop using these before restoring more behavior:

- `tile_t::is_submitted_for_loading`;
- `tile_t::need_keep_in_cache`;
- `tile_t::need_gpu_residency`;
- `tile_t::read_region_refcount`;
- `tile_t::pixels`;
- `tile_t::is_cached`;
- `tile_t::texture`;
- `request_tiles()` as a public scheduling API;
- `load_tile_task_t::completion_queue`;
- `load_tile_task_t::completion_event_kind`.

Temporary compatibility is acceptable only when it has a clear deletion path.
Do not add new semantics to these old fields.

## New API Shape

The public surface should be cache/demand oriented:

```c
tile_cache_t* tile_cache_create(image_t* image);
void tile_cache_destroy(tile_cache_t* cache);

void tile_cache_request_viewer_tiles(image_t* image, tile_cache_viewer_request_t* request);

void tile_cache_pin_region_cpu_tiles(image_t* image, i32 level, bounds2i tile_bounds);
void tile_cache_unpin_region_cpu_tiles(image_t* image, i32 level, bounds2i tile_bounds);
bool tile_cache_region_cpu_ready(image_t* image, i32 level, bounds2i tile_bounds);

void tile_cache_drain_results(image_t* image);
```

Worker tasks should describe backend decode work only:

```c
typedef struct load_tile_task_t {
    i32 resource_id;
    image_t* image;
    i32 level;
    i32 tile_index;
    i32 priority;
    bool8 invert_colors;
    bool8 may_discard_if_stale;
    i32 generation;
} load_tile_task_t;
```

Worker completions should be posted to a long-lived result queue owned by
`tile_cache_t` or `image_t`, never to a caller-owned queue.

## Migration Phases

1. Add `tile_cache.{c,h}` and attach it to `image_t` lifetime.
2. Remove the previous `tile_streamer` files and integration.
3. Move CPU pixel ownership from `tile_t` into `tile_cache_tile_t`.
4. Move GPU texture ownership from `tile_t` into `tile_cache_tile_t`.
5. Replace viewer tile submission with `tile_cache_request_viewer_tiles()`.
6. Replace `image_read_region()` tile loading with hard CPU pins and cache-state
   waiting.
7. Change tile workers to post decoded results into the cache result queue.
8. Reintroduce stale cancellation only after hard CPU demand and cache waiting are
   correct.
9. Add eviction only after CPU/GPU pins are explicit and tested.

## Testing Priorities

Start with deterministic cache-state tests rather than GUI tests:

- viewer request schedules a missing tile;
- viewer-only stale request can be ignored;
- hard CPU demand joins an in-flight decode;
- hard CPU demand prevents stale cancellation;
- `image_read_region()` waits on cache state, not a private worker queue;
- overlapping CPU pins keep pixels alive until the last unpin;
- unpinned CPU/GPU resident tiles are eligible for eviction;
- pinned CPU/GPU resident tiles are never evicted.

The first integration test should exercise `image_read_region()` reading a tile
that was already requested by the viewer. That is the case the old design could
not make safe without queue ownership hacks.
