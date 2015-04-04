//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/kernels/virtual.hpp>

namespace dynd {
namespace nd {

  struct copy_ck : virtual_ck<copy_ck> {
    static intptr_t
    instantiate(const arrfunc_type_data *self, const arrfunc_type *af_tp,
                char *data, void *ckb, intptr_t ckb_offset,
                const ndt::type &dst_tp, const char *dst_arrmeta, intptr_t nsrc,
                const ndt::type *src_tp, const char *const *src_arrmeta,
                kernel_request_t kernreq, const eval::eval_context *ectx,
                const nd::array &kwds,
                const std::map<nd::string, ndt::type> &tp_vars);

    static void
    resolve_dst_type(const arrfunc_type_data *self, const arrfunc_type *self_tp,
                     char *data, ndt::type &dst_tp, intptr_t nsrc,
                     const ndt::type *src_tp, const nd::array &kwds,
                     const std::map<nd::string, ndt::type> &tp_vars);
  };

} // namespace dynd::nd
} // namespace dynd