/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_PYTHON_PY_EXECUTABLE_H_
#define TENSORFLOW_COMPILER_XLA_PYTHON_PY_EXECUTABLE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "tensorflow/compiler/xla/pjrt/pjrt_client.h"
#include "tensorflow/compiler/xla/python/py_buffer.h"
#include "tensorflow/compiler/xla/python/py_client.h"
#include "tensorflow/compiler/xla/python/traceback.h"
#include "tensorflow/compiler/xla/statusor.h"
#include "tensorflow/compiler/xla/types.h"

namespace xla {

class PyToken {
 public:
  PyToken() = default;
  explicit PyToken(PjRtFuture<Status> future) : future_(std::move(future)) {}

  static PyToken ReadyPyToken() {
    return PyToken(PjRtFuture<Status>(OkStatus()));
  }

  Status Await();

 private:
  PjRtFuture<Status> future_;
};

// Python wrapper around PjRtExecutable. We use a wrapper class:
// a) to keep the PyClient alive via a std::shared_ptr<>
// b) to add Python-specific functionality.
class PyExecutable : public std::enable_shared_from_this<PyExecutable> {
 public:
  PyExecutable(std::shared_ptr<PyClient> client,
               std::unique_ptr<PjRtLoadedExecutable> executable,
               std::shared_ptr<Traceback> traceback,
               std::optional<std::string> fingerprint,
               std::vector<pybind11::capsule> host_callbacks);
  ~PyExecutable();

  std::shared_ptr<PyClient> client() const { return client_; }
  std::shared_ptr<PjRtLoadedExecutable> executable() const {
    return executable_;
  }

  absl::Span<const PjRtLoadedExecutable::LogicalDeviceIds>
  addressable_device_logical_ids() const {
    return executable_->addressable_device_logical_ids();
  }

  std::vector<ClientAndPtr<PjRtDevice>> AddressableDevices() const;

  int64_t SizeOfGeneratedCodeInBytes() const {
    return executable_->SizeOfGeneratedCodeInBytes();
  }

  StatusOr<CompiledMemoryStats> GetCompiledMemoryStats() const {
    return executable_->GetCompiledMemoryStats();
  }

  void Delete() { return executable_->Delete(); }

  bool is_deleted() { return executable_->IsDeleted(); }

  StatusOr<std::vector<PyBuffer::object>> Execute(
      absl::Span<PyBuffer::object const> args);

  StatusOr<std::pair<std::vector<PyBuffer::object>, PyToken>> ExecuteWithToken(
      absl::Span<PyBuffer::object const> args);

  // Takes args indexed by argid then deviceid, transposes them, and passes to
  // PjRtExecutable::Execute. The result is similarly transposed back into the
  // argid,deviceid format.
  // args is [num_args x num_devices].
  StatusOr<std::vector<std::vector<PyBuffer::object>>>
  ExecuteShardedOnLocalDevices(
      absl::Span<const std::vector<PyBuffer::object>> args);

  StatusOr<std::pair<std::vector<std::vector<PyBuffer::object>>,
                     std::vector<PyToken>>>
  ExecuteShardedOnLocalDevicesWithTokens(
      absl::Span<const std::vector<PyBuffer::object>> args);

  StatusOr<std::vector<std::shared_ptr<HloModule>>> HloModules() const;

  Traceback* traceback() { return traceback_.get(); }

  const PjRtLoadedExecutable& pjrt_executable() const { return *executable_; }

  PjRtLoadedExecutable* mutable_pjrt_executable() const {
    return executable_.get();
  }
  const ExecuteOptions& options() const { return options_; }
  const std::optional<std::string>& fingerprint() const { return fingerprint_; }

  // Keep `obj` alive as long as PyExecutable.
  void KeepAlive(pybind11::object obj);

 private:
  StatusOr<std::pair<std::vector<PyBuffer::object>, PyToken>> ExecuteInternal(
      absl::Span<PyBuffer::object const> args,
      std::optional<std::vector<PjRtFuture<Status>>>& returned_futures);
  StatusOr<std::pair<std::vector<std::vector<PyBuffer::object>>,
                     std::vector<PyToken>>>
  ExecuteShardedOnLocalDevicesInternal(
      absl::Span<const std::vector<PyBuffer::object>> args,
      std::optional<std::vector<PjRtFuture<Status>>>& returned_futures);

  friend class PyClient;

  std::shared_ptr<PyClient> client_;
  std::shared_ptr<PjRtLoadedExecutable> executable_;
  std::shared_ptr<Traceback> traceback_;

  // Identical executables (i.e. representing the same program) will have the
  // same fingerprint. nullopt on platforms or executables where fingerprints
  // aren't implemented.
  std::optional<std::string> fingerprint_;

  // The python callbacks implemented using send/recv support.
  std::vector<pybind11::capsule> host_callbacks_;

  // The options to pass to `executable_.Execute`.
  ExecuteOptions options_;

  // Python objects to keep alive as requested by user.
  std::vector<pybind11::object> keepalives_;

  // Doubly-linked list of all executables known to the client. Protected by the
  // GIL.
  PyExecutable* next_;
  PyExecutable* prev_;
};

}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_PYTHON_PY_EXECUTABLE_H_
