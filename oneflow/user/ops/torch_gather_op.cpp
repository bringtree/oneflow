/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/framework/framework.h"

namespace oneflow {

namespace user_op{
REGISTER_USER_OP("torch_gather")
    .Input("input")
    .Input("index")
    .Output("out")
    .Attr("dim", UserOpAttrType::kAtInt64)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const TensorDesc* in = ctx->TensorDesc4ArgNameAndIndex("input", 0);
      CHECK_GT_OR_RETURN(in->shape().NumAxes(), 0);

      const TensorDesc* index = ctx->TensorDesc4ArgNameAndIndex("index", 0);
      CHECK_GT_OR_RETURN(index->shape().NumAxes(), 0);
      CHECK_OR_RETURN(IsIndexDataType(index->data_type()));

      const int64_t dim = ctx->Attr<int64_t>("dim");
      CHECK_GE_OR_RETURN(dim, 0);

      // check in and index tensor, only axis "dim" differs
      // ...

      user_op::TensorDesc* out = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      *out->mut_shape() = index->shape();
      *out->mut_data_type() = in->data_type();

      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("scatter_dim_add")
    .Input("src")
    .Input("index")
    .Output("out")
    .Attr("dim", UserOpAttrType::kAtInt64)
    .Attr("shape", UserOpAttrType::kAtShape)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const TensorDesc* src = ctx->TensorDesc4ArgNameAndIndex("src", 0);
      const TensorDesc* index = ctx->TensorDesc4ArgNameAndIndex("index", 0);
      const Shape& params_shape = ctx->Attr<Shape>("shape");

      // check src, index params_shape
      // ...

      user_op::TensorDesc* out = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      *out->mut_shape() = params_shape;
      *out->mut_data_type() = src->data_type();

      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("torch_gather")
    .SetBackwardOpConfGenFn(
    [](user_op::BackwardOpConfContext* ctx) {

      const auto op_grad_name = ctx->FwOp().op_name() + "_grad";

      ctx->DefineOp(op_grad_name, 
        [&ctx](user_op::BackwardOpBuilder& builder) {
          return builder.OpTypeName("scatter_dim_add") // scatter_dim_add(dim, index, src) -> output
              .InputBind("index", ctx->FwOp().input("index", 0)) //scatter.index <- gather.input
              .InputBind("src", ctx->FwOp().output_grad("out", 0)) //scatter.src <- grad of gather.out
              .Output("out")
              .Attr("dim", ctx->FwOp().attr<int64_t>("dim"))
              .Attr("shape", ctx->FwOp().attr<Shape>("shape"))
              .Build();
        });

      ctx->FwOp().InputGradBind(user_op::OpArg("input", 0), 
        [&ctx, &op_grad_name]() -> const std::string& {
          return ctx->GetOp(op_grad_name)
                .output("out", 0);
        });
  });

} // namespace user_op

}  // namespace oneflow