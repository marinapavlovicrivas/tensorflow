/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

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

#ifdef INTEL_MKL

#include <vector>
#include <algorithm>
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/numeric_op.h"
#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/util/tensor_format.h"
#include "tensorflow/core/kernels/ops_util.h"
#include "tensorflow/core/platform/macros.h"

#include "third_party/mkl/include/mkl_dnn_types.h"
#include "third_party/mkl/include/mkl_dnn.h"
#include "tensorflow/core/util/mkl_util.h"

namespace tensorflow {
  typedef Eigen::ThreadPoolDevice CPUDevice;

///////////////////////////////////////////////////////////
//               Op kernel
///////////////////////////////////////////////////////////

template <typename Device, typename T>
class MklToTfOp : public OpKernel {
 public:
  explicit MklToTfOp(OpKernelConstruction* context) : OpKernel(context) {
    OP_REQUIRES_OK(context, context->GetAttr("data_format", &data_format_str));
    OP_REQUIRES_OK(context, context->GetAttr("T", &op_data_type));
  }

  void Compute(OpKernelContext* context) override {
    // 1. Check that input tensor is in MKL format.
    const Tensor& input_tensor = MklGetInput(context, 0);
    MklShape input_shape;
    GetMklShape(context, 0, &input_shape);

    // if input is already in Tf format, then just copy input tensor to output.
    if (!input_shape.IsMklTensor()) {
      context->set_output(0, input_tensor);
      VLOG(1) << "MKLToTFConversion: No conversion needed, "
              << "copying input to output";
      return;
    }

    // Check that input data type is same as operator data type and that it is
    // same as output data type.
    DataType input_data_type  = input_type(0);
    DataType output_data_type = output_type(0);
    CHECK_EQ(op_data_type, input_data_type);
    CHECK_EQ(op_data_type, output_data_type);

    // We need to recreate Tf tensor shape based on sizes and strides.
    // Ideally, we should know what the data_format is, but that attribute
    // to this op is not reliable. So below, we rely of sorting logic where
    // we sort strides first and then sizes.
    TensorShape output_shape;
    std::vector<std::pair<int, int>> shape_size;
    for (size_t i = 0; i < input_shape.GetDimension(); i++) {
      VLOG(1) << "Size: " << input_shape.GetSizes()[i]
              << ", Strides: " << input_shape.GetStrides()[i];
      shape_size.push_back(std::make_pair(input_shape.GetSizes()[i],
                                          input_shape.GetStrides()[i]));
    }

    std::sort(shape_size.begin(), shape_size.end(), [](
      std::pair<int, int > a, std::pair<int, int> b) {
      return (a.second > b.second) ||
             (a.second == b.second && a.first > b.first);
    });

    for (std::pair<int, int> s_s : shape_size) {
      VLOG(1) << "Added dimension: " << s_s.first;
      output_shape.AddDim(s_s.first);
    }

    // Allocate output tensor.
    Tensor* output_tensor = NULL;
    OP_REQUIRES_OK(context,
                   context->allocate_output(0, output_shape, &output_tensor));

    // 3. Get input and output layout pointers.
    dnnLayout_t output_layout = static_cast<dnnLayout_t>(
                                  input_shape.GetTfLayout());

    // 4. Execute DNNConversion.
    void *input_buffer  = static_cast<void*>(const_cast<T*>(
                              input_tensor.flat<T>().data()));
    void *output_buffer = static_cast<void*>(const_cast<T*>(
                              output_tensor->flat<T>().data()));
    input_shape.GetConvertedFlatData(output_layout, input_buffer,
                                     output_buffer);

    VLOG(1) << "MKLToTFConversion complete successfully.";
  }

 private:
    /// Data format of the operation
    string data_format_str;

    /// Data type of the operation
    DataType op_data_type;
};

///////////////////////////////////////////////////////////
//               Register kernel
///////////////////////////////////////////////////////////

#define REGISTER_CPU(T) \
  REGISTER_KERNEL_BUILDER( \
    Name("MklToTf").Device(DEVICE_CPU).TypeConstraint<T>("T") \
    .Label(mkl_layer_registry::kMklLayerLabel), \
    MklToTfOp<CPUDevice, T>);

TF_CALL_float(REGISTER_CPU);
#undef REGISTER_CPU
}  // namespace tensorflow
#endif /* INTEL_MKL */
