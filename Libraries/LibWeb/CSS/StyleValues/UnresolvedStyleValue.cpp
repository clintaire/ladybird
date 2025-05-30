/*
 * Copyright (c) 2018-2020, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2021-2024, Sam Atkins <sam@ladybird.org>
 * Copyright (c) 2022-2023, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibWeb/CSS/Serialize.h>
#include <LibWeb/CSS/StyleValues/UnresolvedStyleValue.h>

namespace Web::CSS {

String UnresolvedStyleValue::to_string(SerializationMode) const
{
    if (m_original_source_text.has_value())
        return *m_original_source_text;

    return serialize_a_series_of_component_values(m_values, InsertWhitespace::Yes);
}

bool UnresolvedStyleValue::equals(CSSStyleValue const& other) const
{
    if (type() != other.type())
        return false;
    // This is a case where comparing the strings actually makes sense.
    return to_string(SerializationMode::Normal) == other.to_string(SerializationMode::Normal);
}

}
