//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

// Implement a string concatenation kernel

#pragma once

#include <dynd/string.hpp>
#include <dynd/callable.hpp>
#include <dynd/kernels/kernel_builder.hpp>
#include <dynd/types/string_type.hpp>

namespace dynd {
  namespace nd {

    struct string_concatenation_kernel
      : base_kernel<string_concatenation_kernel, 2> {

      void single(char *dst, char *const *src) {
        dynd::string *d = reinterpret_cast<dynd::string *>(dst);
        const dynd::string *const *s = reinterpret_cast<const dynd::string *const *>(src);

        string_concat(2, d, s);
      }
    };

  } // namespace nd

  namespace ndt {

    template<>
    struct traits<dynd::nd::string_concatenation_kernel> {
      static type equivalent() { return callable_type::make(type(string_id), {type(string_id), type(string_id)}); }
    };

  } // namespace ndt

} // namespace dynd
