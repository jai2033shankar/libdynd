//
// Copyright (C) 2011-14 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

namespace dynd {

template <typename T>
struct back;

template <typename I, size_t J>
struct at;

template <typename I, size_t J>
struct from;

template <typename I, size_t J>
struct to;

template <typename I, typename J>
struct join;

template <typename I, typename J>
struct take;

template <typename... I>
struct alternate;

template <typename T, T... I>
struct integer_sequence {
  static_assert(std::is_integral<T>::value,
                "integer_sequence must be instantiated with an integral type");

  enum { size = sizeof...(I) };
  typedef T type;
};

template <size_t... I>
using index_sequence = integer_sequence<size_t, I...>;

template <typename T, T I0, T... I>
struct at<integer_sequence<T, I0, I...>, 0> {
  enum { value = I0 };
};

template <typename T, T I0, T... I, size_t J>
struct at<integer_sequence<T, I0, I...>, J> {
  enum { value = at<integer_sequence<T, I...>, J - 1>::value };
};

template <typename T, T... I, T... J>
struct join<integer_sequence<T, I...>, integer_sequence<T, J...>> {
  typedef integer_sequence<T, I..., J...> type;
};

namespace detail {

  template <int flags, typename T, T...>
  struct make_integer_sequence;

  template <int flags, typename T, T stop>
  struct make_integer_sequence<flags, T, stop>
      : make_integer_sequence<flags, T, 0, stop> {
  };

  template <int flags, typename T, T start, T stop>
  struct make_integer_sequence<flags, T, start, stop>
      : make_integer_sequence<flags, T, start, stop, 1> {
  };

  template <typename T, T start, T stop, T step>
      struct make_integer_sequence<-1, T, start, stop, step>
      : make_integer_sequence < start<stop, T, start, stop, step> {
  };

  template <typename T, T start, T stop, T step>
  struct make_integer_sequence<0, T, start, stop, step> {
    typedef integer_sequence<T> type;
  };

  template <typename T, T start, T stop, T step>
  struct make_integer_sequence<1, T, start, stop, step> {
    enum { next = start + step };

    typedef typename join < integer_sequence<T, start>,
        typename make_integer_sequence<
            next<stop, T, next, stop, step>::type>::type type;
  };

} // namespace detail

template <typename T, T... I>
using make_integer_sequence =
    typename detail::make_integer_sequence<-1, T, I...>::type;

template <size_t... I>
using make_index_sequence = make_integer_sequence<size_t, I...>;

template <typename... T>
struct type_sequence {
  enum { size = sizeof...(T) };
};

template <typename T>
struct back<type_sequence<T>> {
  typedef T type;
};

template <typename T0, typename... T>
struct back<type_sequence<T0, T...>> {
  typedef typename back<type_sequence<T...>>::type type;
};

template <typename T0, typename... T>
struct at<type_sequence<T0, T...>, 0> {
  typedef T0 type;
};

template <size_t I, typename T0, typename... T>
struct at<type_sequence<T0, T...>, I> {
  typedef typename at<type_sequence<T...>, I - 1>::type type;
};

template <typename T0, typename... T>
struct from<type_sequence<T0, T...>, 0> {
  typedef type_sequence<T0, T...> type;
};

template <typename T0, typename... T>
struct from<type_sequence<T0, T...>, 1> {
  typedef type_sequence<T...> type;
};

template <typename T0, typename... T, size_t I>
struct from<type_sequence<T0, T...>, I> {
  typedef typename from<type_sequence<T...>, I - 1>::type type;
};

template <typename T0, typename... T>
struct to<type_sequence<T0, T...>, 0> {
  typedef type_sequence<> type;
};

template <typename T0, typename... T>
struct to<type_sequence<T0, T...>, 1> {
  typedef type_sequence<T0> type;
};

template <typename T0, typename... T, size_t I>
struct to<type_sequence<T0, T...>, I> {
  typedef typename join<type_sequence<T0>, typename to<type_sequence<T...>,
                                                       I - 1>::type>::type type;
};

template <typename... T, typename... U>
struct join<type_sequence<T...>, type_sequence<U...>> {
  typedef type_sequence<T..., U...> type;
};

template <typename... T, size_t... I>
struct take<type_sequence<T...>, index_sequence<I...>> {
  typedef type_sequence<typename at<type_sequence<T...>, I>::type...> type;
};

template <typename T>
struct alternate<integer_sequence<T>, integer_sequence<T>> {
  typedef integer_sequence<T> type;
};

// This case shouldn't be necessary, but was added to work around bug:
// https://connect.microsoft.com/VisualStudio/feedback/details/1045397/recursive-variadic-template-metaprogram-ice-when-pack-gets-to-empty-size
template <typename T, T I0, T J0>
struct alternate<integer_sequence<T, I0>, integer_sequence<T, J0>> {
  typedef integer_sequence<T, I0, J0> type;
};

template <typename T, T I0, T... I, T J0, T... J>
struct alternate<integer_sequence<T, I0, I...>, integer_sequence<T, J0, J...>> {
  typedef typename join<
      integer_sequence<T, I0, J0>,
      typename alternate<integer_sequence<T, I...>,
                         integer_sequence<T, J...>>::type>::type type;
};

template <typename T>
struct alternate<integer_sequence<T>, integer_sequence<T>, integer_sequence<T>,
                 integer_sequence<T>> {
  typedef integer_sequence<T> type;
};

// Another workaround
template <typename T, T I0, T J0, T K0, T L0>
struct alternate<integer_sequence<T, I0>, integer_sequence<T, J0>,
                 integer_sequence<T, K0>, integer_sequence<T, L0>> {
  typedef integer_sequence<T, I0, J0, K0, L0> type;
};

template <typename T, T I0, T... I, T J0, T... J, T K0, T... K, T L0, T... L>
struct alternate<integer_sequence<T, I0, I...>, integer_sequence<T, J0, J...>,
                 integer_sequence<T, K0, K...>, integer_sequence<T, L0, L...>> {
  typedef typename join<
      integer_sequence<T, I0, J0, K0, L0>,
      typename alternate<integer_sequence<T, I...>, integer_sequence<T, J...>,
                         integer_sequence<T, K...>,
                         integer_sequence<T, L...>>::type>::type type;
};

} // namespace dynd