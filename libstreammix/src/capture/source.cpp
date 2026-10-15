/**
 * sm::Source — Static Definitions
 */

#include "streammix/source.h"

namespace sm {

std::atomic<uint64_t> Source::s_nextId { 1 };

} // namespace sm
