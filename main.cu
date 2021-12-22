#include <timemory/timemory.hpp>

#include "cupti_perfworks.hpp"

namespace comp = tim::component;

using bundle_t = tim::component_tuple<comp::trip_count, comp::cupti_perfworks, comp::papi_tuple<PAPI_TOT_CYC, PAPI_TOT_INS>>;

template <typename T>
TIMEMORY_GLOBAL_FUNCTION void KERNEL_A(T* begin, int n) {
  auto range =
      tim::device::grid_strided_range<tim::device::default_device, 0>(n);
  for (int i = range.begin(); i < range.end(); i += range.stride()) {
    if (i < n) *(begin + i) += 2.0f * n;
  }
}

int main(int argc, char** argv) {
  tim::timemory_init(argc, argv);

  float* data = tim::device::gpu::alloc<float>(96);
  float* data2 = tim::device::gpu::alloc<float>(192);
  tim::device::params<tim::device::default_device> params(2, 64, 0, 0);

  bundle_t _v{"main"};
  _v.start();
  KERNEL_A<float><<<2, 64, 0>>>(data, 96);
  bundle_t _v2{"inner"};
  _v2.start();
  KERNEL_A<float><<<2, 64, 0>>>(data2, 192);
  _v2.stop();
  _v.stop();

  tim::timemory_finalize();
}
