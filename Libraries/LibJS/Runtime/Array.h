/*
 * Copyright (c) 2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2020-2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Assertions.h>
#include <AK/Concepts.h>
#include <AK/Function.h>
#include <AK/Span.h>
#include <AK/Vector.h>
#include <LibJS/Export.h>
#include <LibJS/Runtime/Completion.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/VM.h>

namespace JS {

class JS_API Array : public Object {
    JS_OBJECT(Array, Object);
    GC_DECLARE_ALLOCATOR(Array);

public:
    static ThrowCompletionOr<GC::Ref<Array>> create(Realm&, u64 length, Object* prototype = nullptr);
    static GC::Ref<Array> create_from(Realm&, ReadonlySpan<Value>);

    template<size_t N>
    static GC::Ref<Array> create_from(Realm& realm, Value const (&values)[N])
    {
        return create_from(realm, ReadonlySpan<Value> { values, N });
    }

    // Non-standard but equivalent to CreateArrayFromList.
    template<typename T>
    static GC::Ref<Array> create_from(Realm& realm, ReadonlySpan<T> elements, Function<Value(T const&)> map_fn)
    {
        auto values = GC::RootVector<Value> { realm.heap() };
        values.ensure_capacity(elements.size());
        for (auto const& element : elements)
            values.append(map_fn(element));

        return Array::create_from(realm, values);
    }

    virtual ~Array() override = default;

    virtual ThrowCompletionOr<Optional<PropertyDescriptor>> internal_get_own_property(PropertyKey const&) const override final;
    virtual ThrowCompletionOr<bool> internal_set(PropertyKey const&, Value value, Value receiver, CacheablePropertyMetadata*, PropertyLookupPhase) override;
    virtual ThrowCompletionOr<bool> internal_define_own_property(PropertyKey const&, PropertyDescriptor const&, Optional<PropertyDescriptor>* precomputed_get_own_property = nullptr) override final;
    virtual ThrowCompletionOr<bool> internal_has_property(PropertyKey const&) const override final;
    virtual ThrowCompletionOr<bool> internal_delete(PropertyKey const&) override;
    virtual ThrowCompletionOr<GC::RootVector<Value>> internal_own_property_keys() const override final;

    [[nodiscard]] bool length_is_writable() const { return m_length_writable; }

    void set_is_proxy_target(bool is_proxy_target) { m_is_proxy_target = is_proxy_target; }

    virtual void visit_edges(Cell::Visitor& visitor) override;

protected:
    explicit Array(Realm& realm, Object& prototype);

private:
    virtual bool is_array_exotic_object() const final { return true; }

    ThrowCompletionOr<bool> set_length(PropertyDescriptor const&);

    GC::Ref<Realm> m_realm;
    bool m_length_writable { true };
    bool m_is_proxy_target { false };
};

template<>
inline bool Object::fast_is<Array>() const { return is_array_exotic_object(); }

enum class Holes {
    SkipHoles,
    ReadThroughHoles,
};

ThrowCompletionOr<GC::RootVector<Value>> sort_indexed_properties(VM&, Object const&, size_t length, Function<ThrowCompletionOr<double>(Value, Value)> const& sort_compare, Holes holes);
ThrowCompletionOr<double> compare_array_elements(VM&, Value x, Value y, FunctionObject* comparefn);

}
