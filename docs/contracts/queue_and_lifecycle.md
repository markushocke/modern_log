# Queue And Lifecycle Contract

Queue defaults:
- bounded queue measured in records
- FIFO ordering preserved
- single consumer drain in the first async slice
- wake-up threshold smaller than or equal to batch size

Flush and shutdown:
- explicit flush drains all pending records before returning
- periodic flush is an additional trigger, not a weaker drain path
- shutdown drains pending records before worker exit
- destructor delegates to shutdown
- drain steps follow requested/idle reschedule semantics aligned with modern_trace
- async sink dispatch writes full drain batches and flushes once per drain step

Backpressure policies:
- drop_oldest
- drop_newest
- block_producer
- sample_debug_logs
- priority_preserve_errors

Sink failures increment counters and do not silently mutate queue ordering.