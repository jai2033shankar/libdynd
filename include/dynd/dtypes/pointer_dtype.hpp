//
// Copyright (C) 2011-12, Dynamic NDArray Developers
// BSD 2-Clause License, see LICENSE.txt
//

/**
 * The pointer dtype contains C/C++ raw pointers
 * pointing at data in other memory_blocks, using
 * blockrefs to manage the memory.
 *
 * This dtype operates in a "gather/scatter" fashion,
 * exposing itself as an expression dtype whose expression
 * copies the data to/from the pointer targets.
 */

#ifndef _DYND__POINTER_DTYPE_HPP_
#define _DYND__POINTER_DTYPE_HPP_

#include <dynd/dtype.hpp>
#include <dynd/dtypes/void_pointer_dtype.hpp>

namespace dynd {

struct pointer_dtype_metadata {
    /**
     * A reference to the memory block which contains the data.
     */
    memory_block_data *blockref;
    /* Each pointed-to destination is offset by this amount */
    intptr_t offset;
};

class pointer_dtype : public extended_expression_dtype {
    dtype m_target_dtype;
    static dtype m_void_pointer_dtype;

public:
    pointer_dtype(const dtype& target_dtype);

    type_id_t get_type_id() const {
        return pointer_type_id;
    }
    dtype_kind_t get_kind() const {
        return expression_kind;
    }
    // Expose the storage traits here
    size_t get_alignment() const {
        return sizeof(void *);
    }
    size_t get_data_size() const {
        return sizeof(void *);
    }

    const dtype& get_value_dtype() const {
        return m_target_dtype;
    }
    const dtype& get_operand_dtype() const {
        if (m_target_dtype.get_type_id() == pointer_type_id) {
            return m_target_dtype;
        } else {
            return m_void_pointer_dtype;
        }
    }

    void print_data(std::ostream& o, const char *metadata, const char *data) const;

    void print_dtype(std::ostream& o) const;

    // This is about the native storage, buffering code needs to check whether
    // the value_dtype is an object type separately.
    dtype_memory_management_t get_memory_management() const {
        return blockref_memory_management;
    }

    bool is_scalar() const;
    bool is_uniform_dim() const;
    bool is_expression() const;
    void transform_child_dtypes(dtype_transform_fn_t transform_fn, const void *extra,
                    dtype& out_transformed_dtype, bool& out_was_transformed) const;
    dtype get_canonical_dtype() const;

    dtype apply_linear_index(int nindices, const irange *indices, int current_i, const dtype& root_dt) const;
    intptr_t apply_linear_index(int nindices, const irange *indices, char *data, const char *metadata,
                    const dtype& result_dtype, char *out_metadata,
                    memory_block_data *embedded_reference,
                    int current_i, const dtype& root_dt) const;

    int get_undim() const;
    dtype get_dtype_at_dimension(char **inout_metadata, int i, int total_ndim = 0) const;

    intptr_t get_dim_size(const char *data, const char *metadata) const;
    void get_shape(int i, intptr_t *out_shape) const;

    bool is_lossless_assignment(const dtype& dst_dt, const dtype& src_dt) const;

    void get_single_compare_kernel(single_compare_kernel_instance& out_kernel) const;

    bool operator==(const extended_dtype& rhs) const;

    // Converts to/from the storage's value dtype
    void get_operand_to_value_kernel(const eval::eval_context *ectx,
                            kernel_instance<unary_operation_pair_t>& out_borrowed_kernel) const;
    void get_value_to_operand_kernel(const eval::eval_context *ectx,
                            kernel_instance<unary_operation_pair_t>& out_borrowed_kernel) const;
    dtype with_replaced_storage_dtype(const dtype& replacement_dtype) const;

    size_t get_metadata_size() const;
    void metadata_default_construct(char *metadata, int ndim, const intptr_t* shape) const;
    void metadata_copy_construct(char *dst_metadata, const char *src_metadata, memory_block_data *embedded_reference) const;
    void metadata_reset_buffers(char *metadata) const;
    void metadata_finalize_buffers(char *metadata) const;
    void metadata_destruct(char *metadata) const;
    void metadata_debug_print(const char *metadata, std::ostream& o, const std::string& indent) const;
};

inline dtype make_pointer_dtype(const dtype& target_dtype) {
    if (target_dtype.get_type_id() != void_type_id) {
        return dtype(new pointer_dtype(target_dtype));
    } else {
        return dtype(new void_pointer_dtype());
    }
}

template<typename Tnative>
dtype make_pointer_dtype() {
    return make_pointer_dtype(make_dtype<Tnative>());
}

} // namespace dynd

#endif // _DYND__POINTER_DTYPE_HPP_
