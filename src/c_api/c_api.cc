// Copyright (c) 2014-2021 by Contributors
#include <rabit/rabit.h>
#include <rabit/c_api.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <memory>

#include "xgboost/base.h"
#include "xgboost/data.h"
#include "xgboost/host_device_vector.h"
#include "xgboost/learner.h"
#include "xgboost/c_api.h"
#include "xgboost/logging.h"
#include "xgboost/version_config.h"
#include "xgboost/json.h"
#include "xgboost/global_config.h"

#include "c_api_error.h"
#include "c_api_utils.h"
#include "../common/io.h"
#include "../common/charconv.h"
#include "../data/adapter.h"
#include "../data/simple_dmatrix.h"
#include "../data/proxy_dmatrix.h"

using namespace xgboost; // NOLINT(*);

XGB_DLL void XGBoostVersion(int* major, int* minor, int* patch) {
  if (major) {
    *major = XGBOOST_VER_MAJOR;
  }
  if (minor) {
    *minor = XGBOOST_VER_MINOR;
  }
  if (patch) {
    *patch = XGBOOST_VER_PATCH;
  }
}

XGB_DLL int XGBRegisterLogCallback(void (*callback)(const char*)) {
  API_BEGIN();
  LogCallbackRegistry* registry = LogCallbackRegistryStore::Get();
  registry->Register(callback);
  API_END();
}

XGB_DLL int XGBSetGlobalConfig(const char* json_str) {
  API_BEGIN();
  Json config{Json::Load(StringView{json_str})};
  for (auto& items : get<Object>(config)) {
    switch (items.second.GetValue().Type()) {
    case xgboost::Value::ValueKind::kInteger: {
      items.second = String{std::to_string(get<Integer const>(items.second))};
      break;
    }
    case xgboost::Value::ValueKind::kBoolean: {
      if (get<Boolean const>(items.second)) {
        items.second = String{"true"};
      } else {
        items.second = String{"false"};
      }
      break;
    }
    case xgboost::Value::ValueKind::kNumber: {
      auto n = get<Number const>(items.second);
      char chars[NumericLimits<float>::kToCharsSize];
      auto ec = to_chars(chars, chars + sizeof(chars), n).ec;
      CHECK(ec == std::errc());
      items.second = String{chars};
      break;
    }
    default:
      break;
    }
  }
  auto unknown = FromJson(config, GlobalConfigThreadLocalStore::Get());
  if (!unknown.empty()) {
    std::stringstream ss;
    ss << "Unknown global parameters: { ";
    size_t i = 0;
    for (auto const& item : unknown) {
      ss << item.first;
      i++;
      if (i != unknown.size()) {
        ss << ", ";
      }
    }
    LOG(FATAL) << ss.str()  << " }";
  }
  API_END();
}

using GlobalConfigAPIThreadLocalStore = dmlc::ThreadLocalStore<XGBAPIThreadLocalEntry>;

XGB_DLL int XGBGetGlobalConfig(const char** json_str) {
  API_BEGIN();
  auto const& global_config = *GlobalConfigThreadLocalStore::Get();
  Json config {ToJson(global_config)};
  auto const* mgr = global_config.__MANAGER__();

  for (auto& item : get<Object>(config)) {
    auto const &str = get<String const>(item.second);
    auto const &name = item.first;
    auto e = mgr->Find(name);
    CHECK(e);

    if (dynamic_cast<dmlc::parameter::FieldEntry<int32_t> const*>(e) ||
        dynamic_cast<dmlc::parameter::FieldEntry<int64_t> const*>(e) ||
        dynamic_cast<dmlc::parameter::FieldEntry<uint32_t> const*>(e) ||
        dynamic_cast<dmlc::parameter::FieldEntry<uint64_t> const*>(e)) {
      auto i = std::strtoimax(str.data(), nullptr, 10);
      CHECK_LE(i, static_cast<intmax_t>(std::numeric_limits<int64_t>::max()));
      item.second = Integer(static_cast<int64_t>(i));
    } else if (dynamic_cast<dmlc::parameter::FieldEntry<float> const *>(e) ||
               dynamic_cast<dmlc::parameter::FieldEntry<double> const *>(e)) {
      float f;
      auto ec = from_chars(str.data(), str.data() + str.size(), f).ec;
      CHECK(ec == std::errc());
      item.second = Number(f);
    } else if (dynamic_cast<dmlc::parameter::FieldEntry<bool> const *>(e)) {
      item.second = Boolean(str != "0");
    }
  }

  auto& local = *GlobalConfigAPIThreadLocalStore::Get();
  Json::Dump(config, &local.ret_str);
  *json_str = local.ret_str.c_str();
  API_END();
}

XGB_DLL int XGDMatrixCreateFromFile(const char *fname,
                                    int silent,
                                    DMatrixHandle *out) {
  API_BEGIN();
  bool load_row_split = false;
  if (rabit::IsDistributed()) {
    LOG(CONSOLE) << "XGBoost distributed mode detected, "
                 << "will split data among workers";
    load_row_split = true;
  }
  *out = new std::shared_ptr<DMatrix>(DMatrix::Load(fname, silent != 0, load_row_split));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromDataIter(
    void *data_handle,                  // a Java iterator
    XGBCallbackDataIterNext *callback,  // C++ callback defined in xgboost4j.cpp
    const char *cache_info, DMatrixHandle *out) {
  API_BEGIN();

  std::string scache;
  if (cache_info != nullptr) {
    scache = cache_info;
  }
  xgboost::data::IteratorAdapter<DataIterHandle, XGBCallbackDataIterNext,
                                 XGBoostBatchCSR> adapter(data_handle, callback);
  *out = new std::shared_ptr<DMatrix> {
    DMatrix::Create(
        &adapter, std::numeric_limits<float>::quiet_NaN(),
        1, scache
    )
  };
  API_END();
}

#ifndef XGBOOST_USE_CUDA
XGB_DLL int XGDMatrixCreateFromArrayInterfaceColumns(char const* c_json_strs,
                                                     bst_float missing,
                                                     int nthread,
                                                     DMatrixHandle* out) {
  API_BEGIN();
  common::AssertGPUSupport();
  API_END();
}

XGB_DLL int XGDMatrixCreateFromArrayInterface(char const* c_json_strs,
                                              bst_float missing,
                                              int nthread,
                                              DMatrixHandle* out) {
  API_BEGIN();
  common::AssertGPUSupport();
  API_END();
}

#endif

// Create from data iterator
XGB_DLL int XGProxyDMatrixCreate(DMatrixHandle* out) {
  API_BEGIN();
  *out = new std::shared_ptr<xgboost::DMatrix>(new xgboost::data::DMatrixProxy);;
  API_END();
}

XGB_DLL int
XGDeviceQuantileDMatrixSetDataCudaArrayInterface(DMatrixHandle handle,
                                                 char const *c_interface_str) {
  API_BEGIN();
  CHECK_HANDLE();
  auto p_m = static_cast<std::shared_ptr<xgboost::DMatrix> *>(handle);
  CHECK(p_m);
  auto m =   static_cast<xgboost::data::DMatrixProxy*>(p_m->get());
  CHECK(m) << "Current DMatrix type does not support set data.";
  m->SetData(c_interface_str);
  API_END();
}

XGB_DLL int
XGDeviceQuantileDMatrixSetDataCudaColumnar(DMatrixHandle handle,
                                           char const *c_interface_str) {
  API_BEGIN();
  CHECK_HANDLE();
  auto p_m = static_cast<std::shared_ptr<xgboost::DMatrix> *>(handle);
  CHECK(p_m);
  auto m =   static_cast<xgboost::data::DMatrixProxy*>(p_m->get());
  CHECK(m) << "Current DMatrix type does not support set data.";
  m->SetData(c_interface_str);
  API_END();
}

XGB_DLL int XGDeviceQuantileDMatrixCreateFromCallback(
    DataIterHandle iter, DMatrixHandle proxy, DataIterResetCallback *reset,
    XGDMatrixCallbackNext *next, float missing, int nthread,
    int max_bin, DMatrixHandle *out) {
  API_BEGIN();
  *out = new std::shared_ptr<xgboost::DMatrix>{
    xgboost::DMatrix::Create(iter, proxy, reset, next, missing, nthread, max_bin)};
  API_END();
}
// End Create from data iterator

XGB_DLL int XGDMatrixCreateFromCSREx(const size_t* indptr,
                                     const unsigned* indices,
                                     const bst_float* data,
                                     size_t nindptr,
                                     size_t nelem,
                                     size_t num_col,
                                     DMatrixHandle* out) {
  API_BEGIN();
  data::CSRAdapter adapter(indptr, indices, data, nindptr - 1, nelem, num_col);
  *out = new std::shared_ptr<DMatrix>(DMatrix::Create(&adapter, std::nan(""), 1));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromCSR(char const *indptr,
                                   char const *indices, char const *data,
                                   xgboost::bst_ulong ncol,
                                   char const* c_json_config,
                                   DMatrixHandle* out) {
  API_BEGIN();
  data::CSRArrayAdapter adapter(StringView{indptr}, StringView{indices},
                                StringView{data}, ncol);
  auto config = Json::Load(StringView{c_json_config});
  float missing = GetMissing(config);
  auto nthread = get<Integer const>(config["nthread"]);
  *out = new std::shared_ptr<DMatrix>(DMatrix::Create(&adapter, missing, nthread));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromCSCEx(const size_t* col_ptr,
                                     const unsigned* indices,
                                     const bst_float* data,
                                     size_t nindptr,
                                     size_t,
                                     size_t num_row,
                                     DMatrixHandle* out) {
  API_BEGIN();
  data::CSCAdapter adapter(col_ptr, indices, data, nindptr - 1, num_row);
  *out = new std::shared_ptr<DMatrix>(DMatrix::Create(&adapter, std::nan(""), 1));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromMat(const bst_float* data,
                                   xgboost::bst_ulong nrow,
                                   xgboost::bst_ulong ncol, bst_float missing,
                                   DMatrixHandle* out) {
  API_BEGIN();
  data::DenseAdapter adapter(data, nrow, ncol);
  *out = new std::shared_ptr<DMatrix>(DMatrix::Create(&adapter, missing, 1));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromMat_omp(const bst_float* data,  // NOLINT
                                       xgboost::bst_ulong nrow,
                                       xgboost::bst_ulong ncol,
                                       bst_float missing, DMatrixHandle* out,
                                       int nthread) {
  API_BEGIN();
  data::DenseAdapter adapter(data, nrow, ncol);
  *out = new std::shared_ptr<DMatrix>(DMatrix::Create(&adapter, missing, nthread));
  API_END();
}

XGB_DLL int XGDMatrixCreateFromDT(void** data, const char** feature_stypes,
                                  xgboost::bst_ulong nrow,
                                  xgboost::bst_ulong ncol, DMatrixHandle* out,
                                  int nthread) {
  API_BEGIN();
  data::DataTableAdapter adapter(data, feature_stypes, nrow, ncol);
  *out = new std::shared_ptr<DMatrix>(
      DMatrix::Create(&adapter, std::nan(""), nthread));
  API_END();
}

XGB_DLL int XGDMatrixSliceDMatrix(DMatrixHandle handle,
                                  const int* idxset,
                                  xgboost::bst_ulong len,
                                  DMatrixHandle* out) {
  return XGDMatrixSliceDMatrixEx(handle, idxset, len, out, 0);
}

XGB_DLL int XGDMatrixSliceDMatrixEx(DMatrixHandle handle,
                                    const int* idxset,
                                    xgboost::bst_ulong len,
                                    DMatrixHandle* out,
                                    int allow_groups) {
  API_BEGIN();
  CHECK_HANDLE();
  if (!allow_groups) {
    CHECK_EQ(static_cast<std::shared_ptr<DMatrix>*>(handle)
                 ->get()
                 ->Info()
                 .group_ptr_.size(),
             0U)
        << "slice does not support group structure";
  }
  DMatrix* dmat = static_cast<std::shared_ptr<DMatrix>*>(handle)->get();
  *out = new std::shared_ptr<DMatrix>(
      dmat->Slice({idxset, static_cast<std::size_t>(len)}));
  API_END();
}

XGB_DLL int XGDMatrixFree(DMatrixHandle handle) {
  API_BEGIN();
  CHECK_HANDLE();
  delete static_cast<std::shared_ptr<DMatrix>*>(handle);
  API_END();
}

XGB_DLL int XGDMatrixSaveBinary(DMatrixHandle handle, const char* fname,
                                int) {
  API_BEGIN();
  CHECK_HANDLE();
  auto dmat = static_cast<std::shared_ptr<DMatrix>*>(handle)->get();
  if (data::SimpleDMatrix* derived = dynamic_cast<data::SimpleDMatrix*>(dmat)) {
    derived->SaveToLocalFile(fname);
  } else {
    LOG(FATAL) << "binary saving only supported by SimpleDMatrix";
  }
  API_END();
}

XGB_DLL int XGDMatrixSetFloatInfo(DMatrixHandle handle,
                                  const char* field,
                                  const bst_float* info,
                                  xgboost::bst_ulong len) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<std::shared_ptr<DMatrix>*>(handle)
      ->get()->Info().SetInfo(field, info, xgboost::DataType::kFloat32, len);
  API_END();
}

XGB_DLL int XGDMatrixSetInfoFromInterface(DMatrixHandle handle,
                                          char const* field,
                                          char const* interface_c_str) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<std::shared_ptr<DMatrix>*>(handle)
      ->get()->Info().SetInfo(field, interface_c_str);
  API_END();
}

XGB_DLL int XGDMatrixSetUIntInfo(DMatrixHandle handle,
                                 const char* field,
                                 const unsigned* info,
                                 xgboost::bst_ulong len) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<std::shared_ptr<DMatrix>*>(handle)
      ->get()->Info().SetInfo(field, info, xgboost::DataType::kUInt32, len);
  API_END();
}

XGB_DLL int XGDMatrixSetStrFeatureInfo(DMatrixHandle handle, const char *field,
                                       const char **c_info,
                                       const xgboost::bst_ulong size) {
  API_BEGIN();
  CHECK_HANDLE();
  auto &info = static_cast<std::shared_ptr<DMatrix> *>(handle)->get()->Info();
  info.SetFeatureInfo(field, c_info, size);
  API_END();
}

XGB_DLL int XGDMatrixGetStrFeatureInfo(DMatrixHandle handle, const char *field,
                                       xgboost::bst_ulong *len,
                                       const char ***out_features) {
  API_BEGIN();
  CHECK_HANDLE();
  auto m = *static_cast<std::shared_ptr<DMatrix>*>(handle);
  auto &info = static_cast<std::shared_ptr<DMatrix> *>(handle)->get()->Info();

  std::vector<const char *> &charp_vecs = m->GetThreadLocal().ret_vec_charp;
  std::vector<std::string> &str_vecs = m->GetThreadLocal().ret_vec_str;

  info.GetFeatureInfo(field, &str_vecs);

  charp_vecs.resize(str_vecs.size());
  for (size_t i = 0; i < str_vecs.size(); ++i) {
    charp_vecs[i] = str_vecs[i].c_str();
  }
  *out_features = dmlc::BeginPtr(charp_vecs);
  *len = static_cast<xgboost::bst_ulong>(charp_vecs.size());
  API_END();
}

XGB_DLL int XGDMatrixSetDenseInfo(DMatrixHandle handle, const char *field,
                                  void *data, xgboost::bst_ulong size,
                                  int type) {
  API_BEGIN();
  CHECK_HANDLE();
  auto &info = static_cast<std::shared_ptr<DMatrix> *>(handle)->get()->Info();
  CHECK(type >= 1 && type <= 4);
  info.SetInfo(field, data, static_cast<DataType>(type), size);
  API_END();
}

XGB_DLL int XGDMatrixSetGroup(DMatrixHandle handle,
                              const unsigned* group,
                              xgboost::bst_ulong len) {
  API_BEGIN();
  CHECK_HANDLE();
  LOG(WARNING) << "XGDMatrixSetGroup is deprecated, use `XGDMatrixSetUIntInfo` instead.";
  static_cast<std::shared_ptr<DMatrix>*>(handle)
      ->get()->Info().SetInfo("group", group, xgboost::DataType::kUInt32, len);
  API_END();
}

XGB_DLL int XGDMatrixGetFloatInfo(const DMatrixHandle handle,
                                  const char* field,
                                  xgboost::bst_ulong* out_len,
                                  const bst_float** out_dptr) {
  API_BEGIN();
  CHECK_HANDLE();
  const MetaInfo& info = static_cast<std::shared_ptr<DMatrix>*>(handle)->get()->Info();
  info.GetInfo(field, out_len, DataType::kFloat32, reinterpret_cast<void const**>(out_dptr));
  API_END();
}

XGB_DLL int XGDMatrixGetUIntInfo(const DMatrixHandle handle,
                                 const char *field,
                                 xgboost::bst_ulong *out_len,
                                 const unsigned **out_dptr) {
  API_BEGIN();
  CHECK_HANDLE();
  const MetaInfo& info = static_cast<std::shared_ptr<DMatrix>*>(handle)->get()->Info();
  info.GetInfo(field, out_len, DataType::kUInt32, reinterpret_cast<void const**>(out_dptr));
  API_END();
}

XGB_DLL int XGDMatrixNumRow(const DMatrixHandle handle,
                            xgboost::bst_ulong *out) {
  API_BEGIN();
  CHECK_HANDLE();
  *out = static_cast<xgboost::bst_ulong>(
      static_cast<std::shared_ptr<DMatrix>*>(handle)->get()->Info().num_row_);
  API_END();
}

XGB_DLL int XGDMatrixNumCol(const DMatrixHandle handle,
                            xgboost::bst_ulong *out) {
  API_BEGIN();
  CHECK_HANDLE();
  *out = static_cast<xgboost::bst_ulong>(
      static_cast<std::shared_ptr<DMatrix>*>(handle)->get()->Info().num_col_);
  API_END();
}

// xgboost implementation
XGB_DLL int XGBoosterCreate(const DMatrixHandle dmats[],
                            xgboost::bst_ulong len,
                            BoosterHandle *out) {
  API_BEGIN();
  std::vector<std::shared_ptr<DMatrix> > mats;
  for (xgboost::bst_ulong i = 0; i < len; ++i) {
    mats.push_back(*static_cast<std::shared_ptr<DMatrix>*>(dmats[i]));
  }
  *out = Learner::Create(mats);
  API_END();
}

XGB_DLL int XGBoosterFree(BoosterHandle handle) {
  API_BEGIN();
  CHECK_HANDLE();
  delete static_cast<Learner*>(handle);
  API_END();
}

XGB_DLL int XGBoosterSetParam(BoosterHandle handle,
                              const char *name,
                              const char *value) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<Learner*>(handle)->SetParam(name, value);
  API_END();
}

XGB_DLL int XGBoosterGetNumFeature(BoosterHandle handle,
                                   xgboost::bst_ulong *out) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<Learner*>(handle)->Configure();
  *out = static_cast<Learner*>(handle)->GetNumFeature();
  API_END();
}

XGB_DLL int XGBoosterBoostedRounds(BoosterHandle handle, int* out) {
  API_BEGIN();
  CHECK_HANDLE();
  static_cast<Learner*>(handle)->Configure();
  *out = static_cast<Learner*>(handle)->BoostedRounds();
  API_END();
}

XGB_DLL int XGBoosterLoadJsonConfig(BoosterHandle handle, char const* json_parameters) {
  API_BEGIN();
  CHECK_HANDLE();
  Json config { Json::Load(StringView{json_parameters}) };
  static_cast<Learner*>(handle)->LoadConfig(config);
  API_END();
}

XGB_DLL int XGBoosterSaveJsonConfig(BoosterHandle handle,
                                    xgboost::bst_ulong *out_len,
                                    char const** out_str) {
  API_BEGIN();
  CHECK_HANDLE();
  Json config { Object() };
  auto* learner = static_cast<Learner*>(handle);
  learner->Configure();
  learner->SaveConfig(&config);
  std::string& raw_str = learner->GetThreadLocal().ret_str;
  Json::Dump(config, &raw_str);
  *out_str = raw_str.c_str();
  *out_len = static_cast<xgboost::bst_ulong>(raw_str.length());
  API_END();
}

XGB_DLL int XGBoosterUpdateOneIter(BoosterHandle handle,
                                   int iter,
                                   DMatrixHandle dtrain) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* bst = static_cast<Learner*>(handle);
  auto *dtr =
      static_cast<std::shared_ptr<DMatrix>*>(dtrain);

  bst->UpdateOneIter(iter, *dtr);
  API_END();
}

XGB_DLL int XGBoosterBoostOneIter(BoosterHandle handle,
                                  DMatrixHandle dtrain,
                                  bst_float *grad,
                                  bst_float *hess,
                                  xgboost::bst_ulong len) {
  HostDeviceVector<GradientPair> tmp_gpair;
  API_BEGIN();
  CHECK_HANDLE();
  auto* bst = static_cast<Learner*>(handle);
  auto* dtr =
      static_cast<std::shared_ptr<DMatrix>*>(dtrain);
  tmp_gpair.Resize(len);
  std::vector<GradientPair>& tmp_gpair_h = tmp_gpair.HostVector();
  for (xgboost::bst_ulong i = 0; i < len; ++i) {
    tmp_gpair_h[i] = GradientPair(grad[i], hess[i]);
  }

  bst->BoostOneIter(0, *dtr, &tmp_gpair);
  API_END();
}

XGB_DLL int XGBoosterEvalOneIter(BoosterHandle handle,
                                 int iter,
                                 DMatrixHandle dmats[],
                                 const char* evnames[],
                                 xgboost::bst_ulong len,
                                 const char** out_str) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* bst = static_cast<Learner*>(handle);
  std::string& eval_str = bst->GetThreadLocal().ret_str;

  std::vector<std::shared_ptr<DMatrix>> data_sets;
  std::vector<std::string> data_names;

  for (xgboost::bst_ulong i = 0; i < len; ++i) {
    data_sets.push_back(*static_cast<std::shared_ptr<DMatrix>*>(dmats[i]));
    data_names.emplace_back(evnames[i]);
  }

  eval_str = bst->EvalOneIter(iter, data_sets, data_names);
  *out_str = eval_str.c_str();
  API_END();
}

XGB_DLL int XGBoosterPredict(BoosterHandle handle,
                             DMatrixHandle dmat,
                             int option_mask,
                             unsigned ntree_limit,
                             int training,
                             xgboost::bst_ulong *len,
                             const bst_float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  auto *learner = static_cast<Learner*>(handle);
  auto& entry = learner->GetThreadLocal().prediction_entry;
  auto iteration_end = GetIterationFromTreeLimit(ntree_limit, learner);
  learner->Predict(*static_cast<std::shared_ptr<DMatrix> *>(dmat),
                   (option_mask & 1) != 0, &entry.predictions, 0, iteration_end,
                   static_cast<bool>(training), (option_mask & 2) != 0,
                   (option_mask & 4) != 0, (option_mask & 8) != 0,
                   (option_mask & 16) != 0);
  *out_result = dmlc::BeginPtr(entry.predictions.ConstHostVector());
  *len = static_cast<xgboost::bst_ulong>(entry.predictions.Size());
  API_END();
}

XGB_DLL int XGBoosterPredictFromDMatrix(BoosterHandle handle,
                                        DMatrixHandle dmat,
                                        char const* c_json_config,
                                        xgboost::bst_ulong const **out_shape,
                                        xgboost::bst_ulong *out_dim,
                                        bst_float const **out_result) {
  API_BEGIN();
  if (handle == nullptr) {
    LOG(FATAL) << "Booster has not been intialized or has already been disposed.";
  }
  if (dmat == nullptr) {
    LOG(FATAL) << "DMatrix has not been intialized or has already been disposed.";
  }
  auto config = Json::Load(StringView{c_json_config});

  auto *learner = static_cast<Learner*>(handle);
  auto& entry = learner->GetThreadLocal().prediction_entry;
  auto p_m = *static_cast<std::shared_ptr<DMatrix> *>(dmat);
  auto type = PredictionType(get<Integer const>(config["type"]));
  auto iteration_begin = get<Integer const>(config["iteration_begin"]);
  auto iteration_end = get<Integer const>(config["iteration_end"]);
  bool approximate = type == PredictionType::kApproxContribution ||
                     type == PredictionType::kApproxInteraction;
  bool contribs = type == PredictionType::kContribution ||
                  type == PredictionType::kApproxContribution;
  bool interactions = type == PredictionType::kInteraction ||
                      type == PredictionType::kApproxInteraction;
  bool training = get<Boolean const>(config["training"]);
  learner->Predict(p_m, type == PredictionType::kMargin, &entry.predictions,
                   iteration_begin, iteration_end, training,
                   type == PredictionType::kLeaf, contribs, approximate,
                   interactions);
  *out_result = dmlc::BeginPtr(entry.predictions.ConstHostVector());
  auto &shape = learner->GetThreadLocal().prediction_shape;
  auto chunksize = p_m->Info().num_row_ == 0 ? 0 : entry.predictions.Size() / p_m->Info().num_row_;
  auto rounds = iteration_end - iteration_begin;
  rounds = rounds == 0 ? learner->BoostedRounds() : rounds;
  // Determine shape
  bool strict_shape = get<Boolean const>(config["strict_shape"]);
  CalcPredictShape(strict_shape, type, p_m->Info().num_row_,
                   p_m->Info().num_col_, chunksize, learner->Groups(), rounds,
                   &shape, out_dim);
  *out_shape = dmlc::BeginPtr(shape);
  API_END();
}

template <typename T>
void InplacePredictImplCore(std::shared_ptr<T> x, std::shared_ptr<DMatrix> p_m,
                            Learner *learner,
                            xgboost::PredictionType type,
                            float missing,
                            size_t n_rows, size_t n_cols,
                            size_t iteration_begin, size_t iteration_end,
                            bool strict_shape,
                            xgboost::bst_ulong const **out_shape,
                            xgboost::bst_ulong *out_dim, const float **out_result) {
  HostDeviceVector<float>* p_predt { nullptr };
  learner->InplacePredict(x, p_m, type, missing, &p_predt, iteration_begin, iteration_end);
  CHECK(p_predt);
  auto &shape = learner->GetThreadLocal().prediction_shape;
  auto chunksize = n_rows == 0 ? 0 : p_predt->Size() / n_rows;
  CalcPredictShape(strict_shape, type, n_rows, n_cols, chunksize, learner->Groups(),
                   learner->BoostedRounds(), &shape, out_dim);
  *out_result = dmlc::BeginPtr(p_predt->HostVector());
  *out_shape = dmlc::BeginPtr(shape);

  printf("InplacePredictImplCore shape = %u, dim = %u\n", **out_shape, *out_dim);
}

template <typename T>
void InplacePredictImpl(std::shared_ptr<T> x, std::shared_ptr<DMatrix> p_m,
                        char const *c_json_config, Learner *learner,
                        size_t n_rows, size_t n_cols,
                        xgboost::bst_ulong const **out_shape,
                        xgboost::bst_ulong *out_dim, const float **out_result) {
  auto config = Json::Load(StringView{c_json_config});
  CHECK_EQ(get<Integer const>(config["cache_id"]), 0) << "Cache ID is not supported yet";

  auto type = PredictionType(get<Integer const>(config["type"]));
  float missing = GetMissing(config);
  int iteration_begin = get<Integer const>(config["iteration_begin"]);
  int iteration_end   = get<Integer const>(config["iteration_end"]);
  bool strict_shape = get<Boolean const>(config["strict_shape"]);
  InplacePredictImplCore(x, p_m, learner, type, missing, n_rows, n_cols,
                         iteration_begin, iteration_end, strict_shape, out_shape, out_dim, out_result);
}

XGB_DLL int XGBoosterInplacePredict(BoosterHandle handle,
                                    const float *data,
                                    size_t num_rows,
                                    size_t num_features,
                                    int option_mask,
                                    const xgboost::bst_ulong **len,
                                    const bst_float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  xgboost::bst_ulong out_dim;
  std::shared_ptr<xgboost::data::DenseAdapter> x{new xgboost::data::DenseAdapter(data, num_rows, num_features)};
  auto *learner = static_cast<xgboost::Learner *>(handle);
//  InplacePredictImplCore(x, nullptr, learner, (xgboost::PredictionType)0, NAN, num_rows, num_features, 0, 0, true, &out_shape, len, out_result);
  InplacePredictImplCore(x, nullptr, learner, (xgboost::PredictionType)0, NAN, num_rows, num_features, 0, 0, true, len, &out_dim, out_result);
  printf("XGBoosterInplacePredict len = %u, dim = %u\n", *len, out_dim);
  API_END();
}

// A hidden API as cache id is not being supported yet.
XGB_DLL int XGBoosterPredictFromDense(BoosterHandle handle,
                                      char const *array_interface,
                                      char const *c_json_config,
                                      DMatrixHandle m,
                                      xgboost::bst_ulong const **out_shape,
                                      xgboost::bst_ulong *out_dim,
                                      const float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  std::shared_ptr<xgboost::data::ArrayAdapter> x{
      new xgboost::data::ArrayAdapter(StringView{array_interface})};
  std::shared_ptr<DMatrix> p_m {nullptr};
  if (m) {
    p_m = *static_cast<std::shared_ptr<DMatrix> *>(m);
  }
  auto *learner = static_cast<xgboost::Learner *>(handle);
  InplacePredictImpl(x, p_m, c_json_config, learner, x->NumRows(),
                     x->NumColumns(), out_shape, out_dim, out_result);
  API_END();
}

// A hidden API as cache id is not being supported yet.
XGB_DLL int XGBoosterPredictFromCSR(BoosterHandle handle, char const *indptr,
                                    char const *indices, char const *data,
                                    xgboost::bst_ulong cols,
                                    char const *c_json_config, DMatrixHandle m,
                                    xgboost::bst_ulong const **out_shape,
                                    xgboost::bst_ulong *out_dim,
                                    const float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  std::shared_ptr<xgboost::data::CSRArrayAdapter> x{
      new xgboost::data::CSRArrayAdapter{
          StringView{indptr}, StringView{indices}, StringView{data}, cols}};
  std::shared_ptr<DMatrix> p_m {nullptr};
  if (m) {
    p_m = *static_cast<std::shared_ptr<DMatrix> *>(m);
  }
  auto *learner = static_cast<xgboost::Learner *>(handle);
  InplacePredictImpl(x, p_m, c_json_config, learner, x->NumRows(),
                     x->NumColumns(), out_shape, out_dim, out_result);
  API_END();
}

#if !defined(XGBOOST_USE_CUDA)
XGB_DLL int XGBoosterPredictFromCUDAArray(
    BoosterHandle handle, char const *c_json_strs, char const *c_json_config,
    DMatrixHandle m, xgboost::bst_ulong const **out_shape, xgboost::bst_ulong *out_dim,
    const float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  common::AssertGPUSupport();
  API_END();
}

XGB_DLL int XGBoosterPredictFromCUDAColumnar(
    BoosterHandle handle, char const *c_json_strs, char const *c_json_config,
    DMatrixHandle m, xgboost::bst_ulong const **out_shape, xgboost::bst_ulong *out_dim,
    const float **out_result) {
  API_BEGIN();
  CHECK_HANDLE();
  common::AssertGPUSupport();
  API_END();
}
#endif  // !defined(XGBOOST_USE_CUDA)

XGB_DLL int XGBoosterLoadModel(BoosterHandle handle, const char* fname) {
  API_BEGIN();
  CHECK_HANDLE();
  if (common::FileExtension(fname) == "json") {
    auto str = common::LoadSequentialFile(fname);
    CHECK_GT(str.size(), 2);
    CHECK_EQ(str[0], '{');
    Json in { Json::Load({str.c_str(), str.size()}) };
    static_cast<Learner*>(handle)->LoadModel(in);
  } else {
    std::unique_ptr<dmlc::Stream> fi(dmlc::Stream::Create(fname, "r"));
    static_cast<Learner*>(handle)->LoadModel(fi.get());
  }
  API_END();
}

XGB_DLL int XGBoosterSaveModel(BoosterHandle handle, const char* c_fname) {
  API_BEGIN();
  CHECK_HANDLE();
  std::unique_ptr<dmlc::Stream> fo(dmlc::Stream::Create(c_fname, "w"));
  auto *learner = static_cast<Learner *>(handle);
  learner->Configure();
  if (common::FileExtension(c_fname) == "json") {
    Json out { Object() };
    learner->SaveModel(&out);
    std::string str;
    Json::Dump(out, &str);
    fo->Write(str.c_str(), str.size());
  } else {
    auto *bst = static_cast<Learner*>(handle);
    bst->SaveModel(fo.get());
  }
  API_END();
}

XGB_DLL int XGBoosterLoadModelFromBuffer(BoosterHandle handle,
                                         const void* buf,
                                         xgboost::bst_ulong len) {
  API_BEGIN();
  CHECK_HANDLE();
  common::MemoryFixSizeBuffer fs((void*)buf, len);  // NOLINT(*)
  static_cast<Learner*>(handle)->LoadModel(&fs);
  API_END();
}

XGB_DLL int XGBoosterGetModelRaw(BoosterHandle handle,
                                 xgboost::bst_ulong* out_len,
                                 const char** out_dptr) {
  API_BEGIN();
  CHECK_HANDLE();
  auto *learner = static_cast<Learner*>(handle);
  std::string& raw_str = learner->GetThreadLocal().ret_str;
  raw_str.resize(0);

  common::MemoryBufferStream fo(&raw_str);

  learner->Configure();
  learner->SaveModel(&fo);
  *out_dptr = dmlc::BeginPtr(raw_str);
  *out_len = static_cast<xgboost::bst_ulong>(raw_str.length());
  API_END();
}

// The following two functions are `Load` and `Save` for memory based
// serialization methods. E.g. Python pickle.
XGB_DLL int XGBoosterSerializeToBuffer(BoosterHandle handle,
                                       xgboost::bst_ulong *out_len,
                                       const char **out_dptr) {
  API_BEGIN();
  CHECK_HANDLE();
  auto *learner = static_cast<Learner*>(handle);
  std::string &raw_str = learner->GetThreadLocal().ret_str;
  raw_str.resize(0);
  common::MemoryBufferStream fo(&raw_str);
  learner->Configure();
  learner->Save(&fo);
  *out_dptr = dmlc::BeginPtr(raw_str);
  *out_len = static_cast<xgboost::bst_ulong>(raw_str.length());
  API_END();
}

XGB_DLL int XGBoosterUnserializeFromBuffer(BoosterHandle handle,
                                           const void *buf,
                                           xgboost::bst_ulong len) {
  API_BEGIN();
  CHECK_HANDLE();
  common::MemoryFixSizeBuffer fs((void*)buf, len);  // NOLINT(*)
  static_cast<Learner*>(handle)->Load(&fs);
  API_END();
}

XGB_DLL int XGBoosterLoadRabitCheckpoint(BoosterHandle handle,
                                         int* version) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* bst = static_cast<Learner*>(handle);
  *version = rabit::LoadCheckPoint(bst);
  if (*version != 0) {
    bst->Configure();
  }
  API_END();
}

XGB_DLL int XGBoosterSaveRabitCheckpoint(BoosterHandle handle) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* learner = static_cast<Learner*>(handle);
  learner->Configure();
  if (learner->AllowLazyCheckPoint()) {
    rabit::LazyCheckPoint(learner);
  } else {
    rabit::CheckPoint(learner);
  }
  API_END();
}

XGB_DLL int XGBoosterSlice(BoosterHandle handle, int begin_layer,
                           int end_layer, int step,
                           BoosterHandle *out) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* learner = static_cast<Learner*>(handle);
  bool out_of_bound = false;
  auto p_out = learner->Slice(begin_layer, end_layer, step, &out_of_bound);
  if (out_of_bound) {
    return -2;
  }
  CHECK(p_out);
  *out = p_out;
  API_END();
}

inline void XGBoostDumpModelImpl(BoosterHandle handle, const FeatureMap &fmap,
                                 int with_stats, const char *format,
                                 xgboost::bst_ulong *len,
                                 const char ***out_models) {
  auto *bst = static_cast<Learner*>(handle);
  std::vector<std::string>& str_vecs = bst->GetThreadLocal().ret_vec_str;
  std::vector<const char*>& charp_vecs = bst->GetThreadLocal().ret_vec_charp;
  str_vecs = bst->DumpModel(fmap, with_stats != 0, format);
  charp_vecs.resize(str_vecs.size());
  for (size_t i = 0; i < str_vecs.size(); ++i) {
    charp_vecs[i] = str_vecs[i].c_str();
  }
  *out_models = dmlc::BeginPtr(charp_vecs);
  *len = static_cast<xgboost::bst_ulong>(charp_vecs.size());
}

XGB_DLL int XGBoosterDumpModel(BoosterHandle handle,
                               const char* fmap,
                               int with_stats,
                               xgboost::bst_ulong* len,
                               const char*** out_models) {
  API_BEGIN();
  CHECK_HANDLE();
  return XGBoosterDumpModelEx(handle, fmap, with_stats, "text", len, out_models);
  API_END();
}

XGB_DLL int XGBoosterDumpModelEx(BoosterHandle handle,
                                 const char* fmap,
                                 int with_stats,
                                 const char *format,
                                 xgboost::bst_ulong* len,
                                 const char*** out_models) {
  API_BEGIN();
  CHECK_HANDLE();
  FeatureMap featmap;
  if (strlen(fmap) != 0) {
    std::unique_ptr<dmlc::Stream> fs(
        dmlc::Stream::Create(fmap, "r"));
    dmlc::istream is(fs.get());
    featmap.LoadText(is);
  }
  XGBoostDumpModelImpl(handle, featmap, with_stats, format, len, out_models);
  API_END();
}

XGB_DLL int XGBoosterDumpModelWithFeatures(BoosterHandle handle,
                                           int fnum,
                                           const char** fname,
                                           const char** ftype,
                                           int with_stats,
                                           xgboost::bst_ulong* len,
                                           const char*** out_models) {
  return XGBoosterDumpModelExWithFeatures(handle, fnum, fname, ftype, with_stats,
                                          "text", len, out_models);
}

XGB_DLL int XGBoosterDumpModelExWithFeatures(BoosterHandle handle,
                                             int fnum,
                                             const char** fname,
                                             const char** ftype,
                                             int with_stats,
                                             const char *format,
                                             xgboost::bst_ulong* len,
                                             const char*** out_models) {
  API_BEGIN();
  CHECK_HANDLE();
  FeatureMap featmap;
  for (int i = 0; i < fnum; ++i) {
    featmap.PushBack(i, fname[i], ftype[i]);
  }
  XGBoostDumpModelImpl(handle, featmap, with_stats, format, len, out_models);
  API_END();
}

XGB_DLL int XGBoosterGetAttr(BoosterHandle handle,
                     const char* key,
                     const char** out,
                     int* success) {
  auto* bst = static_cast<Learner*>(handle);
  std::string& ret_str = bst->GetThreadLocal().ret_str;
  API_BEGIN();
  CHECK_HANDLE();
  if (bst->GetAttr(key, &ret_str)) {
    *out = ret_str.c_str();
    *success = 1;
  } else {
    *out = nullptr;
    *success = 0;
  }
  API_END();
}

XGB_DLL int XGBoosterSetAttr(BoosterHandle handle,
                             const char* key,
                             const char* value) {
  API_BEGIN();
  CHECK_HANDLE();
  auto* bst = static_cast<Learner*>(handle);
  if (value == nullptr) {
    bst->DelAttr(key);
  } else {
    bst->SetAttr(key, value);
  }
  API_END();
}

XGB_DLL int XGBoosterGetAttrNames(BoosterHandle handle,
                                  xgboost::bst_ulong* out_len,
                                  const char*** out) {
  API_BEGIN();
  CHECK_HANDLE();
  auto *learner = static_cast<Learner *>(handle);
  std::vector<std::string> &str_vecs = learner->GetThreadLocal().ret_vec_str;
  std::vector<const char *> &charp_vecs =
      learner->GetThreadLocal().ret_vec_charp;
  str_vecs = learner->GetAttrNames();
  charp_vecs.resize(str_vecs.size());
  for (size_t i = 0; i < str_vecs.size(); ++i) {
    charp_vecs[i] = str_vecs[i].c_str();
  }
  *out = dmlc::BeginPtr(charp_vecs);
  *out_len = static_cast<xgboost::bst_ulong>(charp_vecs.size());
  API_END();
}

XGB_DLL int XGBoosterSetStrFeatureInfo(BoosterHandle handle, const char *field,
                                       const char **features,
                                       const xgboost::bst_ulong size) {
  API_BEGIN();
  CHECK_HANDLE();
  auto *learner = static_cast<Learner *>(handle);
  std::vector<std::string> feature_info;
  for (size_t i = 0; i < size; ++i) {
    feature_info.emplace_back(features[i]);
  }
  if (!std::strcmp(field, "feature_name")) {
    learner->SetFeatureNames(feature_info);
  } else if (!std::strcmp(field, "feature_type")) {
    learner->SetFeatureTypes(feature_info);
  } else {
    LOG(FATAL) << "Unknown field for Booster feature info:" << field;
  }
  API_END();
}

XGB_DLL int XGBoosterGetStrFeatureInfo(BoosterHandle handle, const char *field,
                                       xgboost::bst_ulong *len,
                                       const char ***out_features) {
  API_BEGIN();
  CHECK_HANDLE();
  auto const *learner = static_cast<Learner const *>(handle);
  std::vector<const char *> &charp_vecs =
      learner->GetThreadLocal().ret_vec_charp;
  std::vector<std::string> &str_vecs = learner->GetThreadLocal().ret_vec_str;
  if (!std::strcmp(field, "feature_name")) {
    learner->GetFeatureNames(&str_vecs);
  } else if (!std::strcmp(field, "feature_type")) {
    learner->GetFeatureTypes(&str_vecs);
  } else {
    LOG(FATAL) << "Unknown field for Booster feature info:" << field;
  }
  charp_vecs.resize(str_vecs.size());
  for (size_t i = 0; i < str_vecs.size(); ++i) {
    charp_vecs[i] = str_vecs[i].c_str();
  }
  *out_features = dmlc::BeginPtr(charp_vecs);
  *len = static_cast<xgboost::bst_ulong>(charp_vecs.size());
  API_END();
}

// force link rabit
static DMLC_ATTRIBUTE_UNUSED int XGBOOST_LINK_RABIT_C_API_ = RabitLinkTag();
