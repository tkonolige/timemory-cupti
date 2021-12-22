#include "cupti_perfworks.hpp"

#include <cupti_profiler_target.h>
#include <cupti_target.h>
#include <nvperf_cuda_host.h>
#include <nvperf_host.h>
#include <nvperf_target.h>

#if !defined(TIMEMORY_NVPW_API_CALL)
#define TIMEMORY_NVPW_API_CALL(apiFuncCall)                                \
  do {                                                                     \
    NVPA_Status _status = apiFuncCall;                                     \
    if (_status != NVPA_STATUS_SUCCESS) {                                  \
      fprintf(stderr, "%s:%d: error: function %s failed with error %d.\n", \
              __FILE__, __LINE__, #apiFuncCall, _status);                  \
      exit(-1);                                                            \
    }                                                                      \
  } while (0)
#endif

TIMEMORY_DECLARE_COMPONENT(cupti_perfworks_data)

namespace tim {
namespace component {
struct cupti_perfworks_data : base<cupti_perfworks_data, std::vector<double>> {
  // keep this the same
  static std::string label() { return "cupti_perfworks"; }
  static std::string description() { return "cupti_perfworks"; }
  static std::vector<std::string> label_array() {return cupti_perfworks::metric_names;}
  static std::vector<std::string> description_array() {return cupti_perfworks::metric_names;}

  void store(value_type _v) { set_accum(std::move(_v)); }
};
}  // namespace component
}  // namespace tim

TIMEMORY_INITIALIZE_STORAGE(cupti_perfworks_data)

std::vector<uint8_t> tim::component::cupti_perfworks::config_image;
std::vector<uint8_t> tim::component::cupti_perfworks::counter_data_image;
std::vector<uint8_t>
    tim::component::cupti_perfworks::counter_data_scratch_buffer;
std::vector<uint8_t> tim::component::cupti_perfworks::counter_data_image_prefix;
std::vector<uint8_t>
    tim::component::cupti_perfworks::counter_availability_image;
std::vector<std::string> tim::component::cupti_perfworks::metric_names;
CUcontext tim::component::cupti_perfworks::cu_ctx;
std::string tim::component::cupti_perfworks::chip_name;

namespace {
//
// ... CUPTI-specific sample code ...
//
}

//
//
// FIXME: how to handle metric names
tim::component::cupti_perfworks::cupti_perfworks() {}

void tim::component::cupti_perfworks::start() {
  std::cout << "starting " << m_prefix << std::endl;
  CUpti_Profiler_PushRange_Params push_range_params = {
      CUpti_Profiler_PushRange_Params_STRUCT_SIZE};
  push_range_params.ctx = cu_ctx;
  push_range_params.pRangeName = m_prefix;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerPushRange(&push_range_params));
}

void tim::component::cupti_perfworks::stop() {
  std::cout << "stopping " << m_prefix << std::endl;
  CUpti_Profiler_PopRange_Params pop_range_params = {
      CUpti_Profiler_PopRange_Params_STRUCT_SIZE};
  pop_range_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerPopRange(&pop_range_params));
}

void tim::component::cupti_perfworks::global_init() {
  int device_num = 0;  // TODO which device
  int num_ranges = 10;  // TODO
  metric_names = {"sm__warps_active.avg.per_cycle_active",
                  "tpc__cycles_in_frame.max"};

  CUdevice cu_device;
  TIMEMORY_CUDA_DRIVER_API_CALL(cuInit(0));
  TIMEMORY_CUDA_DRIVER_API_CALL(cuDeviceGet(&cu_device, device_num));
  TIMEMORY_CUDA_DRIVER_API_CALL(cuDevicePrimaryCtxRetain(&cu_ctx, cu_device));
  CUpti_Profiler_Initialize_Params profiler_initialize_params = {
      CUpti_Profiler_Initialize_Params_STRUCT_SIZE};
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerInitialize(&profiler_initialize_params));

  CUpti_Device_GetChipName_Params get_chip_name_params = {
      CUpti_Device_GetChipName_Params_STRUCT_SIZE};
  get_chip_name_params.deviceIndex = device_num;
  TIMEMORY_CUPTI_API_CALL(cuptiDeviceGetChipName(&get_chip_name_params));
  chip_name = get_chip_name_params.pChipName;

  // Check which counters are available
  CUpti_Profiler_GetCounterAvailability_Params get_counter_availability_params =
      {CUpti_Profiler_GetCounterAvailability_Params_STRUCT_SIZE};
  get_counter_availability_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerGetCounterAvailability(&get_counter_availability_params));

  counter_availability_image.clear();
  counter_availability_image.resize(
      get_counter_availability_params.counterAvailabilityImageSize);
  get_counter_availability_params.pCounterAvailabilityImage =
      counter_availability_image.data();
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerGetCounterAvailability(&get_counter_availability_params));

  // Generate configuration for metrics, this can also be done offline
  NVPW_InitializeHost_Params initialize_host_params = {
      NVPW_InitializeHost_Params_STRUCT_SIZE};
  TIMEMORY_NVPW_API_CALL(NVPW_InitializeHost(&initialize_host_params));

  NVPW_CUDA_MetricsContext_Create_Params metrics_context_create_params = {
      NVPW_CUDA_MetricsContext_Create_Params_STRUCT_SIZE};
  metrics_context_create_params.pChipName = chip_name.c_str();
  TIMEMORY_NVPW_API_CALL(
      NVPW_CUDA_MetricsContext_Create(&metrics_context_create_params));

  // Collect all dependencies of requested metrics
  std::vector<NVPA_RawMetricRequest> raw_metric_requests;
  std::vector<std::string> temp;
  for (auto& metric_name : metric_names) {
    NVPW_MetricsContext_GetMetricProperties_Begin_Params
        get_metric_properties_begin_params = {
            NVPW_MetricsContext_GetMetricProperties_Begin_Params_STRUCT_SIZE};
    get_metric_properties_begin_params.pMetricsContext =
        metrics_context_create_params.pMetricsContext;
    get_metric_properties_begin_params.pMetricName = metric_name.c_str();

    TIMEMORY_NVPW_API_CALL(NVPW_MetricsContext_GetMetricProperties_Begin(
        &get_metric_properties_begin_params));

    for (const char** ppMetricDependencies =
             get_metric_properties_begin_params.ppRawMetricDependencies;
         *ppMetricDependencies; ++ppMetricDependencies) {
      temp.push_back(*ppMetricDependencies);
    }
    NVPW_MetricsContext_GetMetricProperties_End_Params
        get_metric_properties_end_params = {
            NVPW_MetricsContext_GetMetricProperties_End_Params_STRUCT_SIZE};
    get_metric_properties_end_params.pMetricsContext =
        metrics_context_create_params.pMetricsContext;
    TIMEMORY_NVPW_API_CALL(NVPW_MetricsContext_GetMetricProperties_End(
        &get_metric_properties_end_params));
  }

  for (auto& raw_metric_name : temp) {
    NVPA_RawMetricRequest metricRequest = {NVPA_RAW_METRIC_REQUEST_STRUCT_SIZE};
    metricRequest.pMetricName = raw_metric_name.c_str();
    metricRequest.isolated = true;       // TODO
    metricRequest.keepInstances = true;  // TODO: the cupti samples say this
                                         // should be true due to a bug?
    raw_metric_requests.push_back(metricRequest);
  }

  // destroy metrics context
  NVPW_MetricsContext_Destroy_Params metrics_context_destroy_params = {
      NVPW_MetricsContext_Destroy_Params_STRUCT_SIZE};
  metrics_context_destroy_params.pMetricsContext =
      metrics_context_create_params.pMetricsContext;
  TIMEMORY_NVPW_API_CALL(
      NVPW_MetricsContext_Destroy(&metrics_context_destroy_params));

  NVPA_RawMetricsConfigOptions metrics_config_options = {
      NVPA_RAW_METRICS_CONFIG_OPTIONS_STRUCT_SIZE};
  metrics_config_options.activityKind = NVPA_ACTIVITY_KIND_PROFILER;
  metrics_config_options.pChipName = chip_name.c_str();
  NVPA_RawMetricsConfig* raw_metrics_config;
  TIMEMORY_NVPW_API_CALL(NVPA_RawMetricsConfig_Create(&metrics_config_options,
                                                      &raw_metrics_config));

  NVPW_RawMetricsConfig_SetCounterAvailability_Params
      set_counter_availability_params = {
          NVPW_RawMetricsConfig_SetCounterAvailability_Params_STRUCT_SIZE};
  set_counter_availability_params.pRawMetricsConfig = raw_metrics_config;
  set_counter_availability_params.pCounterAvailabilityImage =
      counter_availability_image.data();
  TIMEMORY_NVPW_API_CALL(NVPW_RawMetricsConfig_SetCounterAvailability(
      &set_counter_availability_params));

  NVPW_RawMetricsConfig_BeginPassGroup_Params begin_pass_group_params = {
      NVPW_RawMetricsConfig_BeginPassGroup_Params_STRUCT_SIZE};
  begin_pass_group_params.pRawMetricsConfig = raw_metrics_config;
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_BeginPassGroup(&begin_pass_group_params));

  NVPW_RawMetricsConfig_AddMetrics_Params add_metrics_params = {
      NVPW_RawMetricsConfig_AddMetrics_Params_STRUCT_SIZE};
  add_metrics_params.pRawMetricsConfig = raw_metrics_config;
  add_metrics_params.pRawMetricRequests = raw_metric_requests.data();
  add_metrics_params.numMetricRequests = raw_metric_requests.size();
  TIMEMORY_NVPW_API_CALL(NVPW_RawMetricsConfig_AddMetrics(&add_metrics_params));

  NVPW_RawMetricsConfig_EndPassGroup_Params end_pass_group_params = {
      NVPW_RawMetricsConfig_EndPassGroup_Params_STRUCT_SIZE};
  end_pass_group_params.pRawMetricsConfig = raw_metrics_config;
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_EndPassGroup(&end_pass_group_params));

  NVPW_RawMetricsConfig_GenerateConfigImage_Params
      generate_config_image_params = {
          NVPW_RawMetricsConfig_GenerateConfigImage_Params_STRUCT_SIZE};
  generate_config_image_params.pRawMetricsConfig = raw_metrics_config;
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_GenerateConfigImage(&generate_config_image_params));

  NVPW_RawMetricsConfig_GetConfigImage_Params get_config_image_params = {
      NVPW_RawMetricsConfig_GetConfigImage_Params_STRUCT_SIZE};
  get_config_image_params.pRawMetricsConfig = raw_metrics_config;
  get_config_image_params.bytesAllocated = 0;
  get_config_image_params.pBuffer = NULL;
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_GetConfigImage(&get_config_image_params));

  config_image.resize(get_config_image_params.bytesCopied);

  get_config_image_params.bytesAllocated = config_image.size();
  get_config_image_params.pBuffer = config_image.data();
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_GetConfigImage(&get_config_image_params));

  // destroy raw_metrics_config
  NVPW_RawMetricsConfig_Destroy_Params raw_metrics_config_destroy_params = {
      NVPW_RawMetricsConfig_Destroy_Params_STRUCT_SIZE};
  raw_metrics_config_destroy_params.pRawMetricsConfig = raw_metrics_config;
  TIMEMORY_NVPW_API_CALL(
      NVPW_RawMetricsConfig_Destroy(&raw_metrics_config_destroy_params));

  NVPW_CounterDataBuilder_Create_Params counter_data_builder_create_params = {
      NVPW_CounterDataBuilder_Create_Params_STRUCT_SIZE};
  counter_data_builder_create_params.pChipName = chip_name.c_str();
  TIMEMORY_NVPW_API_CALL(
      NVPW_CounterDataBuilder_Create(&counter_data_builder_create_params));

  NVPW_CounterDataBuilder_AddMetrics_Params counter_add_metrics_params = {
      NVPW_CounterDataBuilder_AddMetrics_Params_STRUCT_SIZE};
  counter_add_metrics_params.pCounterDataBuilder =
      counter_data_builder_create_params.pCounterDataBuilder;
  counter_add_metrics_params.pRawMetricRequests = raw_metric_requests.data();
  counter_add_metrics_params.numMetricRequests = raw_metric_requests.size();
  TIMEMORY_NVPW_API_CALL(
      NVPW_CounterDataBuilder_AddMetrics(&counter_add_metrics_params));

  NVPW_CounterDataBuilder_GetCounterDataPrefix_Params
      get_counter_data_prefix_params = {
          NVPW_CounterDataBuilder_GetCounterDataPrefix_Params_STRUCT_SIZE};
  get_counter_data_prefix_params.pCounterDataBuilder =
      counter_data_builder_create_params.pCounterDataBuilder;
  get_counter_data_prefix_params.bytesAllocated = 0;
  get_counter_data_prefix_params.pBuffer = NULL;
  TIMEMORY_NVPW_API_CALL(NVPW_CounterDataBuilder_GetCounterDataPrefix(
      &get_counter_data_prefix_params));

  counter_data_image_prefix.resize(get_counter_data_prefix_params.bytesCopied);

  get_counter_data_prefix_params.bytesAllocated =
      counter_data_image_prefix.size();
  get_counter_data_prefix_params.pBuffer = counter_data_image_prefix.data();
  TIMEMORY_NVPW_API_CALL(NVPW_CounterDataBuilder_GetCounterDataPrefix(
      &get_counter_data_prefix_params));

  // destroy counter data builder
  NVPW_CounterDataBuilder_Destroy_Params counter_data_builder_destroy_params = {
      NVPW_CounterDataBuilder_Destroy_Params_STRUCT_SIZE};
  counter_data_builder_destroy_params.pCounterDataBuilder =
      counter_data_builder_create_params.pCounterDataBuilder;
  TIMEMORY_NVPW_API_CALL(
      NVPW_CounterDataBuilder_Destroy(&counter_data_builder_destroy_params));

  CUpti_Profiler_CounterDataImageOptions counter_data_image_options;
  counter_data_image_options.pCounterDataPrefix =
      counter_data_image_prefix.data();
  counter_data_image_options.counterDataPrefixSize =
      counter_data_image_prefix.size();
  counter_data_image_options.maxNumRanges = num_ranges;
  counter_data_image_options.maxNumRangeTreeNodes = num_ranges;
  counter_data_image_options.maxRangeNameLength = 64;

  CUpti_Profiler_CounterDataImage_CalculateSize_Params calculate_size_params = {
      CUpti_Profiler_CounterDataImage_CalculateSize_Params_STRUCT_SIZE};
  calculate_size_params.pOptions = &counter_data_image_options;
  calculate_size_params.sizeofCounterDataImageOptions =
      CUpti_Profiler_CounterDataImageOptions_STRUCT_SIZE;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerCounterDataImageCalculateSize(&calculate_size_params));

  CUpti_Profiler_CounterDataImage_Initialize_Params initialize_params = {
      CUpti_Profiler_CounterDataImage_Initialize_Params_STRUCT_SIZE};
  initialize_params.sizeofCounterDataImageOptions =
      CUpti_Profiler_CounterDataImageOptions_STRUCT_SIZE;
  initialize_params.pOptions = &counter_data_image_options;
  initialize_params.counterDataImageSize =
      calculate_size_params.counterDataImageSize;
  counter_data_image.resize(calculate_size_params.counterDataImageSize);
  initialize_params.pCounterDataImage = counter_data_image.data();
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerCounterDataImageInitialize(&initialize_params));

  CUpti_Profiler_CounterDataImage_CalculateScratchBufferSize_Params
      scratch_buffer_size_params = {
          CUpti_Profiler_CounterDataImage_CalculateScratchBufferSize_Params_STRUCT_SIZE};
  scratch_buffer_size_params.counterDataImageSize =
      calculate_size_params.counterDataImageSize;
  scratch_buffer_size_params.pCounterDataImage =
      initialize_params.pCounterDataImage;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerCounterDataImageCalculateScratchBufferSize(
          &scratch_buffer_size_params));

  counter_data_scratch_buffer.resize(
      scratch_buffer_size_params.counterDataScratchBufferSize);

  CUpti_Profiler_CounterDataImage_InitializeScratchBuffer_Params
      init_scratch_buffer_params = {
          CUpti_Profiler_CounterDataImage_InitializeScratchBuffer_Params_STRUCT_SIZE};
  init_scratch_buffer_params.counterDataImageSize =
      calculate_size_params.counterDataImageSize;
  init_scratch_buffer_params.pCounterDataImage =
      initialize_params.pCounterDataImage;
  init_scratch_buffer_params.counterDataScratchBufferSize =
      scratch_buffer_size_params.counterDataScratchBufferSize;
  init_scratch_buffer_params.pCounterDataScratchBuffer =
      counter_data_scratch_buffer.data();
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerCounterDataImageInitializeScratchBuffer(
      &init_scratch_buffer_params));
  // done allocating space for counters and scratch buffer

  CUpti_Profiler_BeginSession_Params begin_session_params = {
      CUpti_Profiler_BeginSession_Params_STRUCT_SIZE};
  begin_session_params.ctx = cu_ctx;
  begin_session_params.counterDataImageSize = counter_data_image.size();
  begin_session_params.pCounterDataImage = counter_data_image.data();
  begin_session_params.counterDataScratchBufferSize =
      counter_data_scratch_buffer.size();
  begin_session_params.pCounterDataScratchBuffer =
      counter_data_scratch_buffer.data();
  begin_session_params.range = CUPTI_UserRange;
  begin_session_params.replayMode =
      CUPTI_UserReplay;  // TODO: this requires the user to re-run the program
  begin_session_params.maxRangesPerPass = num_ranges;
  begin_session_params.maxLaunchesPerPass = num_ranges;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerBeginSession(&begin_session_params));

  CUpti_Profiler_SetConfig_Params set_config_params = {
      CUpti_Profiler_SetConfig_Params_STRUCT_SIZE};
  set_config_params.ctx = cu_ctx;
  set_config_params.pConfig = config_image.data();
  set_config_params.configSize = config_image.size();
  set_config_params.passIndex = 0;         // TODO what is this?
  set_config_params.minNestingLevel = 2;   // TODO
  set_config_params.numNestingLevels = 10;  // TODO
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerSetConfig(&set_config_params));

  CUpti_Profiler_BeginPass_Params begin_pass_params = {
      CUpti_Profiler_BeginPass_Params_STRUCT_SIZE};
  begin_pass_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerBeginPass(&begin_pass_params));

  CUpti_Profiler_EnableProfiling_Params enable_profiling_params = {
      CUpti_Profiler_EnableProfiling_Params_STRUCT_SIZE};
  enable_profiling_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerEnableProfiling(&enable_profiling_params));
}

void tim::component::cupti_perfworks::global_finalize() {
  static bool _once = false;
  if (_once) return;
  _once = true;

  CUpti_Profiler_DisableProfiling_Params disable_profiling_params = {
      CUpti_Profiler_DisableProfiling_Params_STRUCT_SIZE};
  disable_profiling_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerDisableProfiling(&disable_profiling_params));

  // end pass
  // TODO: check end_pass_params.allPassesSubmitted to check if we need to rerun
  CUpti_Profiler_EndPass_Params end_pass_params = {
      CUpti_Profiler_EndPass_Params_STRUCT_SIZE};
  // end_pass_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerEndPass(&end_pass_params));

  std::cout << "all passes submitted? " << (end_pass_params.allPassesSubmitted ? "true" : "false") << std::endl;

  // flush values, cleanup
  CUpti_Profiler_FlushCounterData_Params flush_counter_data_params = {
      CUpti_Profiler_FlushCounterData_Params_STRUCT_SIZE};
  flush_counter_data_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerFlushCounterData(&flush_counter_data_params));

  CUpti_Profiler_UnsetConfig_Params unset_config_params = {
      CUpti_Profiler_UnsetConfig_Params_STRUCT_SIZE};
  unset_config_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerUnsetConfig(&unset_config_params));

  CUpti_Profiler_EndSession_Params end_session_params = {
      CUpti_Profiler_EndSession_Params_STRUCT_SIZE};
  end_session_params.ctx = cu_ctx;
  TIMEMORY_CUPTI_API_CALL(cuptiProfilerEndSession(&end_session_params));

  CUpti_Profiler_DeInitialize_Params profiler_deinitialize_params = {
      CUpti_Profiler_DeInitialize_Params_STRUCT_SIZE};
  TIMEMORY_CUPTI_API_CALL(
      cuptiProfilerDeInitialize(&profiler_deinitialize_params));

  // read metrics
  NVPW_CUDA_MetricsContext_Create_Params metrics_context_create_params = {
      NVPW_CUDA_MetricsContext_Create_Params_STRUCT_SIZE};
  metrics_context_create_params.pChipName = chip_name.c_str();
  TIMEMORY_NVPW_API_CALL(
      NVPW_CUDA_MetricsContext_Create(&metrics_context_create_params));

  NVPW_CounterData_GetNumRanges_Params get_num_ranges_params = {
      NVPW_CounterData_GetNumRanges_Params_STRUCT_SIZE};
  get_num_ranges_params.pCounterDataImage = counter_data_image.data();
  TIMEMORY_NVPW_API_CALL(NVPW_CounterData_GetNumRanges(&get_num_ranges_params));

  std::vector<const char*> metric_name_ptrs;
  for (auto& metric_name : metric_names) {
    metric_name_ptrs.push_back(metric_name.c_str());
  }

  using bundle_t = lightweight_tuple<cupti_perfworks_data>;

  for (size_t range_index = 0; range_index < get_num_ranges_params.numRanges;
       ++range_index) {
    std::vector<const char*> description_ptrs;

    NVPW_Profiler_CounterData_GetRangeDescriptions_Params
        get_range_desc_params = {
            NVPW_Profiler_CounterData_GetRangeDescriptions_Params_STRUCT_SIZE};
    get_range_desc_params.pCounterDataImage = counter_data_image.data();
    get_range_desc_params.rangeIndex = range_index;
    TIMEMORY_NVPW_API_CALL(
        NVPW_Profiler_CounterData_GetRangeDescriptions(&get_range_desc_params));

    description_ptrs.resize(get_range_desc_params.numDescriptions);

    get_range_desc_params.ppDescriptions = description_ptrs.data();
    TIMEMORY_NVPW_API_CALL(
        NVPW_Profiler_CounterData_GetRangeDescriptions(&get_range_desc_params));

    std::string range_name;
    for (size_t description_index = 0;
         description_index < get_range_desc_params.numDescriptions;
         ++description_index) {
      if (description_index) {
        range_name += "/";
      }
      range_name += description_ptrs[description_index];
    }

    const bool isolated = true;
    std::vector<double> gpu_values;
    gpu_values.resize(metric_names.size());

    NVPW_MetricsContext_SetCounterData_Params set_counter_data_params = {
        NVPW_MetricsContext_SetCounterData_Params_STRUCT_SIZE};
    set_counter_data_params.pMetricsContext =
        metrics_context_create_params.pMetricsContext;
    set_counter_data_params.pCounterDataImage = counter_data_image.data();
    set_counter_data_params.isolated = true;
    set_counter_data_params.rangeIndex = range_index;
    NVPW_MetricsContext_SetCounterData(&set_counter_data_params);

    NVPW_MetricsContext_EvaluateToGpuValues_Params eval_to_gpu_params = {
        NVPW_MetricsContext_EvaluateToGpuValues_Params_STRUCT_SIZE};
    eval_to_gpu_params.pMetricsContext =
        metrics_context_create_params.pMetricsContext;
    eval_to_gpu_params.numMetrics = metric_name_ptrs.size();
    eval_to_gpu_params.ppMetricNames = metric_name_ptrs.data();
    eval_to_gpu_params.pMetricValues = gpu_values.data();
    NVPW_MetricsContext_EvaluateToGpuValues(&eval_to_gpu_params);

    bundle_t _v{range_name};
    _v.push();   // create call-stack entry
    _v.start();  // needed for stop call
    _v.store(gpu_values);
    _v.stop();  // increments lap counter
    _v.pop();   // update call-stack entry
    for (size_t metric_index = 0; metric_index < metric_names.size();
         ++metric_index) {
      std::cout << "rangeName: " << range_name
                << "\tmetricName: " << metric_names[metric_index]
                << "\tgpuValue: " << gpu_values[metric_index] << std::endl;
    }
  }

  NVPW_MetricsContext_Destroy_Params metrics_context_destroy_params = {
      NVPW_MetricsContext_Destroy_Params_STRUCT_SIZE};
  metrics_context_destroy_params.pMetricsContext =
      metrics_context_create_params.pMetricsContext;
  TIMEMORY_NVPW_API_CALL(
      NVPW_MetricsContext_Destroy(&metrics_context_destroy_params));
}
