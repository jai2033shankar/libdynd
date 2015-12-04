//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <time.h>

#include <cerrno>
#include <algorithm>

#include <dynd/func/callable.hpp>
#include <dynd/types/datetime_type.hpp>
#include <dynd/types/time_type.hpp>
#include <dynd/types/date_util.hpp>
#include <dynd/types/property_type.hpp>
#include <dynd/types/string_type.hpp>
#include <dynd/types/unary_expr_type.hpp>
#include <dynd/types/typevar_type.hpp>
#include <dynd/kernels/date_expr_kernels.hpp>
#include <dynd/kernels/datetime_adapter_kernels.hpp>
#include <dynd/exceptions.hpp>
#include <dynd/gfunc/make_gcallable.hpp>
#include <dynd/array_iter.hpp>
#include <dynd/parser_util.hpp>

#include <datetime_strings.h>
#include <datetime_localtime.h>

using namespace std;
using namespace dynd;

ndt::datetime_type::datetime_type(datetime_tz_t timezone)
    : base_type(datetime_type_id, datetime_kind, 8, scalar_align_of<int64_t>::value, type_flag_none, 0, 0, 0),
      m_timezone(timezone)
{
}

ndt::datetime_type::~datetime_type() {}

void ndt::datetime_type::set_cal(const char *DYND_UNUSED(arrmeta), char *data, assign_error_mode errmode, int32_t year,
                                 int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second,
                                 int32_t tick) const
{
  if (errmode != assign_error_nocheck) {
    if (!date_ymd::is_valid(year, month, day)) {
      stringstream ss;
      ss << "invalid input year/month/day " << year << "/" << month << "/" << day;
      throw runtime_error(ss.str());
    }
    if (hour < 0 || hour >= 24) {
      stringstream ss;
      ss << "invalid input hour " << hour << " for " << type(this, true);
      throw runtime_error(ss.str());
    }
    if (minute < 0 || minute >= 60) {
      stringstream ss;
      ss << "invalid input minute " << minute << " for " << type(this, true);
      throw runtime_error(ss.str());
    }
    if (second < 0 || second >= 60) {
      stringstream ss;
      ss << "invalid input second " << second << " for " << type(this, true);
      throw runtime_error(ss.str());
    }
    if (tick < 0 || tick >= 1000000000) {
      stringstream ss;
      ss << "invalid input tick (100*nanosecond) " << tick << " for " << type(this, true);
      throw runtime_error(ss.str());
    }
  }

  datetime_struct dts;
  dts.ymd.year = year;
  dts.ymd.month = month;
  dts.ymd.day = day;
  dts.hmst.hour = hour;
  dts.hmst.minute = minute;
  dts.hmst.second = second;
  dts.hmst.tick = tick;

  *reinterpret_cast<int64_t *>(data) = dts.to_ticks();
}

void ndt::datetime_type::set_from_utf8_string(const char *DYND_UNUSED(arrmeta), char *data, const char *utf8_begin,
                                              const char *utf8_end, const eval::eval_context *ectx) const
{
  datetime_struct dts;
  const char *tz_begin = NULL, *tz_end = NULL;
  dts.set_from_str(utf8_begin, utf8_end, ectx->date_parse_order, ectx->century_window, ectx->errmode, tz_begin, tz_end);
  if (m_timezone != tz_abstract && tz_begin != tz_end) {
    if (m_timezone == tz_utc && (parse::compare_range_to_literal(tz_begin, tz_end, "Z") ||
                                 parse::compare_range_to_literal(tz_begin, tz_end, "UTC"))) {
      // It's a UTC time to a UTC time zone
    }
    else {
      stringstream ss;
      ss << "DyND time zone support is partial, cannot handle ";
      ss.write(tz_begin, tz_end - tz_begin);
      throw runtime_error(ss.str());
    }
  }
  *reinterpret_cast<int64_t *>(data) = dts.to_ticks();
}

void ndt::datetime_type::get_cal(const char *DYND_UNUSED(arrmeta), const char *data, int32_t &out_year,
                                 int32_t &out_month, int32_t &out_day, int32_t &out_hour, int32_t &out_min,
                                 int32_t &out_sec, int32_t &out_tick) const
{
  datetime_struct dts;
  dts.set_from_ticks(*reinterpret_cast<const int64_t *>(data));
  out_year = dts.ymd.year;
  out_month = dts.ymd.month;
  out_day = dts.ymd.day;
  out_hour = dts.hmst.hour;
  out_min = dts.hmst.minute;
  out_sec = dts.hmst.second;
  out_tick = dts.hmst.tick;
}

void ndt::datetime_type::print_data(std::ostream &o, const char *DYND_UNUSED(arrmeta), const char *data) const
{
  datetime_struct dts;
  dts.set_from_ticks(*reinterpret_cast<const int64_t *>(data));
  o << dts.to_str();
  if (m_timezone == tz_utc) {
    o << "Z";
  }
}

void ndt::datetime_type::print_type(std::ostream &o) const
{
  if (m_timezone == tz_abstract) {
    o << "datetime";
  }
  else {
    o << "datetime[tz='";
    switch (m_timezone) {
    case tz_utc:
      o << "UTC";
      break;
    default:
      o << "(invalid " << (int32_t)m_timezone << ")";
      break;
    }
    o << "']";
  }
}

bool ndt::datetime_type::is_lossless_assignment(const type &dst_tp, const type &src_tp) const
{
  if (dst_tp.extended() == this) {
    if (src_tp.extended() == this) {
      return true;
    }
    else if (src_tp.get_type_id() == date_type_id) {
      // There is only one possibility for the datetime type (TODO: timezones!)
      return true;
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }
}

bool ndt::datetime_type::operator==(const base_type &rhs) const
{
  if (this == &rhs) {
    return true;
  }
  else if (rhs.get_type_id() != datetime_type_id) {
    return false;
  }
  else {
    const datetime_type &r = static_cast<const datetime_type &>(rhs);
    // TODO: When "other" timezone data is supported, need to compare them too
    return m_timezone == r.m_timezone;
  }
}

///////// functions on the type

/*
static nd::array fn_type_now(const ndt::type &DYND_UNUSED(dt))
{
  throw runtime_error("TODO: implement datetime.now function");
  // datetime_struct dts;
  // fill_current_local_datetime(&fields);
  // nd::array result = nd::empty(dt);
  //reinterpret_cast<int64_t *>(result.get_readwrite_originptr()) =
  // dt.to_ticks();
  // Make the result immutable (we own the only reference to the data at this
  // point)
  // result.flag_as_immutable();
  // return result;
}
*/

/*
static nd::array fn_type_construct(const ndt::type &DYND_UNUSED(dt),
                                   const nd::array &DYND_UNUSED(year),
                                   const nd::array &DYND_UNUSED(month),
                                   const nd::array &DYND_UNUSED(day))
{
  throw runtime_error("dynd type datetime __construct__");
  // Make this like the date version
}
*/

void ndt::datetime_type::get_dynamic_type_functions(const std::pair<std::string, nd::callable> **out_functions,
                                                    size_t *out_count) const
{
  //  static pair<string, nd::callable> datetime_type_functions[] = {
  /*
        pair<string, gfunc::callable>("now",
                                      gfunc::make_callable(&fn_type_now,
     "self")),
        pair<string, gfunc::callable>(
            "__construct__", gfunc::make_callable(&fn_type_construct, "self",
                                                  "year", "month", "day"))
  */
  //};

  *out_functions = NULL;
  *out_count = 0;
  //      sizeof(datetime_type_functions) / sizeof(datetime_type_functions[0]);
}

///////// properties on the nd::array

struct get_date_kernel : nd::base_kernel<get_date_kernel> {
  nd::array self;

  get_date_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_date_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "date"));
  }
};

struct get_year_kernel : nd::base_kernel<get_year_kernel> {
  nd::array self;

  get_year_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_year_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "year"));
  }
};

struct get_month_kernel : nd::base_kernel<get_month_kernel> {
  nd::array self;

  get_month_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_month_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "month"));
  }
};

struct get_day_kernel : nd::base_kernel<get_day_kernel> {
  nd::array self;

  get_day_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_day_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "day"));
  }
};

struct get_hour_kernel : nd::base_kernel<get_hour_kernel> {
  nd::array self;

  get_hour_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_hour_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "hour"));
  }
};

struct get_minute_kernel : nd::base_kernel<get_minute_kernel> {
  nd::array self;

  get_minute_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_minute_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "minute"));
  }
};

struct get_second_kernel : nd::base_kernel<get_second_kernel> {
  nd::array self;

  get_second_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_second_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "second"));
  }
};

struct get_microsecond_kernel : nd::base_kernel<get_microsecond_kernel> {
  nd::array self;

  get_microsecond_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_microsecond_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "microsecond"));
  }
};

struct get_tick_kernel : nd::base_kernel<get_tick_kernel> {
  nd::array self;

  get_tick_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    get_tick_kernel::make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "tick"));
  }
};

void ndt::datetime_type::get_dynamic_array_properties(const std::pair<std::string, nd::callable> **out_properties,
                                                      size_t *out_count) const
{
  static pair<std::string, nd::callable> date_array_properties[] = {
      pair<std::string, nd::callable>("year", nd::callable::make<get_year_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("month", nd::callable::make<get_month_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("day", nd::callable::make<get_day_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("hour", nd::callable::make<get_hour_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("minute", nd::callable::make<get_minute_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("second", nd::callable::make<get_second_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("microsecond",
                                      nd::callable::make<get_microsecond_kernel>(ndt::type("(self: Any) -> Any"))),
      pair<std::string, nd::callable>("tick", nd::callable::make<get_tick_kernel>(ndt::type("(self: Any) -> Any")))};

  *out_properties = date_array_properties;
  *out_count = sizeof(date_array_properties) / sizeof(date_array_properties[0]);
}

///////// functions on the nd::array

struct to_struct_kernel : nd::base_kernel<to_struct_kernel> {
  nd::array self;

  to_struct_kernel(const nd::array &self) : self(self) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0]).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    make(ckb, kernreq, ckb_offset, kwds[0]);
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n)
  {
    return n.replace_dtype(ndt::property_type::make(n.get_dtype(), "struct"));
  }
};

struct strftime_kernel : nd::base_kernel<strftime_kernel> {
  nd::array self;
  std::string format;

  strftime_kernel(const nd::array &self, std::string format) : self(self), format(format) {}

  void single(nd::array *dst, nd::array *const *DYND_UNUSED(src)) { *dst = helper(self, format); }

  static void resolve_dst_type(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), ndt::type &dst_tp,
                               intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                               intptr_t DYND_UNUSED(nkwd), const nd::array *kwds,
                               const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    dst_tp = helper(kwds[0], kwds[1].as<std::string>()).get_type();
  }

  static intptr_t instantiate(char *DYND_UNUSED(static_data), char *DYND_UNUSED(data), void *ckb, intptr_t ckb_offset,
                              const ndt::type &DYND_UNUSED(dst_tp), const char *DYND_UNUSED(dst_arrmeta),
                              intptr_t DYND_UNUSED(nsrc), const ndt::type *DYND_UNUSED(src_tp),
                              const char *const *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq,
                              const eval::eval_context *DYND_UNUSED(ectx), intptr_t DYND_UNUSED(nkwd),
                              const nd::array *kwds, const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars))
  {
    make(ckb, kernreq, ckb_offset, kwds[0], kwds[1].as<std::string>());
    return ckb_offset;
  }

  static nd::array helper(const nd::array &n, const std::string &format)
  {
    // TODO: Allow 'format' itself to be an array, with broadcasting, etc.
    if (format.empty()) {
      throw runtime_error("format string for strftime should not be empty");
    }
    return n.replace_dtype(
        ndt::unary_expr_type::make(ndt::string_type::make(), n.get_dtype(), make_strftime_kernelgen(format)));
  }
};

void ndt::datetime_type::get_dynamic_array_functions(const std::pair<std::string, nd::callable> **out_functions,
                                                     size_t *out_count) const
{
  static pair<std::string, nd::callable> date_array_functions[] = {
      pair<std::string, nd::callable>("to_struct",
                                      nd::callable::make<to_struct_kernel>(ndt::type("(self: Any) -> Any"))),
//      pair<std::string, nd::callable>(
  //        "strftime", nd::callable::make<strftime_kernel>(ndt::type("(self: Any, format: string) -> Any"))),
  };

  *out_functions = date_array_functions;
  *out_count = sizeof(date_array_functions) / sizeof(date_array_functions[0]);
}

///////// property accessor kernels (used by property_type)

namespace {

struct datetime_get_struct_kernel : nd::base_kernel<datetime_get_struct_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *DYND_UNUSED(dst), char *const *DYND_UNUSED(src))
  {
    throw runtime_error("TODO: get_property_kernel_struct_single");
  }
};

struct datetime_set_struct_kernel : nd::base_kernel<datetime_set_struct_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *DYND_UNUSED(dst), char *const *DYND_UNUSED(src))
  {
    throw runtime_error("TODO: set_property_kernel_struct_single");
  }
};

struct datetime_get_date_kernel : nd::base_kernel<datetime_get_date_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t days = **reinterpret_cast<int64_t *const *>(src);
      if (days < 0) {
        days -= (DYND_TICKS_PER_DAY - 1);
      }
      days /= DYND_TICKS_PER_DAY;

      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(days);
    }
    else {
      throw runtime_error("datetime date property only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_time_kernel : nd::base_kernel<datetime_get_time_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t ticks = **reinterpret_cast<int64_t *const *>(src);
      ticks %= DYND_TICKS_PER_DAY;
      if (ticks < 0) {
        ticks += DYND_TICKS_PER_DAY;
      }
      *reinterpret_cast<int64_t *>(dst) = ticks;
    }
    else {
      throw runtime_error("datetime time property only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_year_kernel : nd::base_kernel<datetime_get_year_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      date_ymd ymd;
      ymd.set_from_ticks(**reinterpret_cast<int64_t *const *>(src));
      *reinterpret_cast<int32_t *>(dst) = ymd.year;
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_month_kernel : nd::base_kernel<datetime_get_month_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      date_ymd ymd;
      ymd.set_from_ticks(**reinterpret_cast<int64_t *const *>(src));
      *reinterpret_cast<int32_t *>(dst) = ymd.month;
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_day_kernel : nd::base_kernel<datetime_get_day_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      date_ymd ymd;
      ymd.set_from_ticks(**reinterpret_cast<int64_t *const *>(src));
      *reinterpret_cast<int32_t *>(dst) = ymd.day;
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_hour_kernel : nd::base_kernel<datetime_get_hour_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t hour = **reinterpret_cast<int64_t *const *>(src) % DYND_TICKS_PER_DAY;
      if (hour < 0) {
        hour += DYND_TICKS_PER_DAY;
      }
      hour /= DYND_TICKS_PER_HOUR;
      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(hour);
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_minute_kernel : nd::base_kernel<datetime_get_minute_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t minute = **reinterpret_cast<int64_t *const *>(src) % DYND_TICKS_PER_HOUR;
      if (minute < 0) {
        minute += DYND_TICKS_PER_HOUR;
      }
      minute /= DYND_TICKS_PER_MINUTE;
      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(minute);
    }
    else {
      throw runtime_error("datetime property access only implemented for UTC and "
                          "abstract timezones");
    }
  }
};

struct datetime_get_second_kernel : nd::base_kernel<datetime_get_second_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t second = **reinterpret_cast<int64_t *const *>(src) % DYND_TICKS_PER_MINUTE;
      if (second < 0) {
        second += DYND_TICKS_PER_MINUTE;
      }
      second /= DYND_TICKS_PER_SECOND;
      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(second);
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_microsecond_kernel : nd::base_kernel<datetime_get_microsecond_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t microsecond = **reinterpret_cast<int64_t *const *>(src) % DYND_TICKS_PER_SECOND;
      if (microsecond < 0) {
        microsecond += DYND_TICKS_PER_SECOND;
      }
      microsecond /= DYND_TICKS_PER_MICROSECOND;
      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(microsecond);
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

struct datetime_get_tick_kernel : nd::base_kernel<datetime_get_tick_kernel, 1> {
  ndt::type datetime_tp;

  void single(char *dst, char *const *src)
  {
    const ndt::datetime_type *dd = reinterpret_cast<const ndt::datetime_type *>(datetime_tp.extended());
    datetime_tz_t tz = dd->get_timezone();
    if (tz == tz_utc || tz == tz_abstract) {
      int64_t tick = **reinterpret_cast<int64_t *const *>(src) % 10000000LL;
      if (tick < 0) {
        tick += 10000000LL;
      }
      *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(tick);
    }
    else {
      throw runtime_error("datetime property access only implemented for "
                          "UTC and abstract timezones");
    }
  }
};

} // anonymous namespace

namespace {
enum date_properties_t {
  datetimeprop_struct,
  datetimeprop_date,
  datetimeprop_time,
  datetimeprop_year,
  datetimeprop_month,
  datetimeprop_day,
  datetimeprop_hour,
  datetimeprop_minute,
  datetimeprop_second,
  datetimeprop_microsecond,
  datetimeprop_tick,
};
}

size_t ndt::datetime_type::get_elwise_property_index(const std::string &property_name) const
{
  if (property_name == "struct") {
    // A read/write property for accessing a datetime as a struct
    return datetimeprop_struct;
  }
  else if (property_name == "date") {
    return datetimeprop_date;
  }
  else if (property_name == "time") {
    return datetimeprop_time;
  }
  else if (property_name == "year") {
    return datetimeprop_year;
  }
  else if (property_name == "month") {
    return datetimeprop_month;
  }
  else if (property_name == "day") {
    return datetimeprop_day;
  }
  else if (property_name == "hour") {
    return datetimeprop_hour;
  }
  else if (property_name == "minute") {
    return datetimeprop_minute;
  }
  else if (property_name == "second") {
    return datetimeprop_second;
  }
  else if (property_name == "microsecond") {
    return datetimeprop_microsecond;
  }
  else if (property_name == "tick") {
    return datetimeprop_tick;
  }
  else {
    stringstream ss;
    ss << "dynd type " << type(this, true) << " does not have a kernel for property " << property_name;
    throw runtime_error(ss.str());
  }
}

ndt::type ndt::datetime_type::get_elwise_property_type(size_t property_index, bool &out_readable,
                                                       bool &out_writable) const
{
  switch (property_index) {
  case datetimeprop_struct:
    out_readable = true;
    out_writable = true;
    return datetime_struct::type();
  case datetimeprop_date:
    out_readable = true;
    out_writable = false;
    return date_type::make();
  case datetimeprop_time:
    out_readable = true;
    out_writable = false;
    return time_type::make(m_timezone);
  default:
    out_readable = true;
    out_writable = false;
    return type::make<int32_t>();
  }
}

size_t ndt::datetime_type::make_elwise_property_getter_kernel(void *ckb, intptr_t ckb_offset,
                                                              const char *DYND_UNUSED(dst_arrmeta),
                                                              const char *DYND_UNUSED(src_arrmeta),
                                                              size_t src_property_index, kernel_request_t kernreq,
                                                              const eval::eval_context *DYND_UNUSED(ectx)) const
{
  switch (src_property_index) {
  case datetimeprop_struct: {
    datetime_get_struct_kernel *e = datetime_get_struct_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_date: {
    datetime_get_date_kernel *e = datetime_get_date_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_time: {
    datetime_get_time_kernel *e = datetime_get_time_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_year: {
    datetime_get_year_kernel *e = datetime_get_year_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_month: {
    datetime_get_month_kernel *e = datetime_get_month_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_day: {
    datetime_get_day_kernel *e = datetime_get_day_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_hour: {
    datetime_get_hour_kernel *e = datetime_get_hour_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_minute: {
    datetime_get_minute_kernel *e = datetime_get_minute_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_second: {
    datetime_get_second_kernel *e = datetime_get_second_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_microsecond: {
    datetime_get_microsecond_kernel *e = datetime_get_microsecond_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  case datetimeprop_tick: {
    datetime_get_tick_kernel *e = datetime_get_tick_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  default:
    stringstream ss;
    ss << "dynd datetime type given an invalid property index" << src_property_index;
    throw runtime_error(ss.str());
  }
  return ckb_offset;
}

size_t ndt::datetime_type::make_elwise_property_setter_kernel(
    void *ckb, intptr_t ckb_offset, const char *DYND_UNUSED(dst_arrmeta), size_t dst_property_index,
    const char *DYND_UNUSED(src_arrmeta), kernel_request_t kernreq, const eval::eval_context *DYND_UNUSED(ectx)) const
{
  switch (dst_property_index) {
  case datetimeprop_struct: {
    datetime_set_struct_kernel *e = datetime_set_struct_kernel::make(ckb, kernreq, ckb_offset);
    e->datetime_tp = type(this, true);
  } break;
  default:
    stringstream ss;
    ss << "dynd datetime type given an invalid property index" << dst_property_index;
    throw runtime_error(ss.str());
  }
  return ckb_offset;
}

bool ndt::datetime_type::adapt_type(const type &operand_tp, const std::string &op, nd::callable &out_forward,
                                    nd::callable &out_reverse) const
{
  return make_datetime_adapter_callable(type(this, true), operand_tp, op, out_forward, out_reverse);
}

bool ndt::datetime_type::reverse_adapt_type(const type &value_tp, const std::string &op, nd::callable &out_forward,
                                            nd::callable &out_reverse) const
{
  // Note that out_reverse and out_forward are swapped compared with
  // adapt_type
  return make_datetime_adapter_callable(type(this, true), value_tp, op, out_reverse, out_forward);
}
