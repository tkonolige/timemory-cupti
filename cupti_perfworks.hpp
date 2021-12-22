#pragma once
#include <timemory/timemory.hpp>

TIMEMORY_DECLARE_COMPONENT(cupti_perfworks)

TIMEMORY_DEFINE_CONCRETE_TRAIT(fini_priority, component::cupti_perfworks,
                               priority_constant<-4>)

namespace tim {
namespace component {
struct cupti_perfworks : base<cupti_perfworks, void> {
  using this_type = cupti_perfworks;
  using value_type = void;
  using base_type = base<this_type, value_type>;
  using storage_type = typename base_type::storage_type;
  using common_type = void;

  static std::string label() { return "cupti_perfworks"; }
  static std::string description() { return "cupti_perfworks"; }
  static std::vector<std::string> label_array() {return metric_names;}
  static std::vector<std::string> description_array() {return metric_names;}
  static const short width = 8; // TODO
  static const short precision = 3; // TODO

  static void global_init();
  static void global_finalize();

  void start();
  void stop();

  void set_prefix(const char* _v) { m_prefix = _v; }

  cupti_perfworks();

  size_t size() const {return metric_names.size();}



 private:
  const char* m_prefix = nullptr;

  static std::vector<uint8_t> config_image;
  static std::vector<uint8_t> counter_data_image;
  static std::vector<uint8_t> counter_data_scratch_buffer;
  static std::vector<uint8_t> counter_data_image_prefix;
  static std::vector<uint8_t> counter_availability_image;
  static std::vector<std::string> metric_names;
  static CUcontext cu_ctx;
  static std::string chip_name;

  friend class cupti_perfworks_data;
};
}  // namespace component
}  // namespace tim
