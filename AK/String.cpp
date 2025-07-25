/*
 * Copyright (c) 2018-2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/Checked.h>
#include <AK/Endian.h>
#include <AK/Enumerate.h>
#include <AK/FlyString.h>
#include <AK/Format.h>
#include <AK/MemMem.h>
#include <AK/Stream.h>
#include <AK/String.h>
#include <AK/Utf16View.h>
#include <AK/Vector.h>
#include <stdlib.h>

#include <simdutf.h>

namespace AK {

String String::from_utf8_with_replacement_character(StringView view, WithBOMHandling with_bom_handling)
{
    if (auto bytes = view.bytes(); with_bom_handling == WithBOMHandling::Yes && bytes.starts_with({ { 0xEF, 0xBB, 0xBF } }))
        view = view.substring_view(3);

    Utf8View utf8_view { view };

    if (utf8_view.validate(AllowLonelySurrogates::No))
        return String::from_utf8_without_validation(view.bytes());

    StringBuilder builder(view.length());

    for (auto code_point : utf8_view) {
        if (is_unicode_surrogate(code_point))
            builder.append_code_point(UnicodeUtils::REPLACEMENT_CODE_POINT);
        else
            builder.append_code_point(code_point);
    }

    return builder.to_string_without_validation();
}

String String::from_utf8_without_validation(ReadonlyBytes bytes)
{
    String result;
    MUST(result.replace_with_new_string(bytes.size(), [&](Bytes buffer) {
        bytes.copy_to(buffer);
        return ErrorOr<void> {};
    }));
    return result;
}

ErrorOr<String> String::from_utf8(StringView view)
{
    if (!Utf8View { view }.validate())
        return Error::from_string_literal("String::from_utf8: Input was not valid UTF-8");

    String result;
    TRY(result.replace_with_new_string(view.length(), [&](Bytes buffer) {
        view.bytes().copy_to(buffer);
        return ErrorOr<void> {};
    }));
    return result;
}

ErrorOr<String> String::from_utf16_le_with_replacement_character(ReadonlyBytes bytes)
{
    if (bytes.is_empty())
        return String {};

    auto const* utf16_data = reinterpret_cast<char16_t const*>(bytes.data());
    auto utf16_length = bytes.size() / 2;

    Vector<char16_t> well_formed_utf16;

    if (!validate_utf16_le(bytes)) {
        well_formed_utf16.resize(bytes.size());

        simdutf::to_well_formed_utf16le(utf16_data, utf16_length, well_formed_utf16.data());
        utf16_data = well_formed_utf16.data();
    }

    auto utf8_length = simdutf::utf8_length_from_utf16le(utf16_data, utf16_length);

    String result;
    TRY(result.replace_with_new_string(utf8_length, [&](Bytes buffer) -> ErrorOr<void> {
        [[maybe_unused]] auto result = simdutf::convert_utf16le_to_utf8(utf16_data, utf16_length, reinterpret_cast<char*>(buffer.data()));
        ASSERT(result == buffer.size());
        return {};
    }));

    return result;
}

ErrorOr<String> String::from_utf16_be_with_replacement_character(ReadonlyBytes bytes)
{
    if (bytes.is_empty())
        return String {};

    auto const* utf16_data = reinterpret_cast<char16_t const*>(bytes.data());
    auto utf16_length = bytes.size() / 2;

    Vector<char16_t> well_formed_utf16;

    if (!validate_utf16_le(bytes)) {
        well_formed_utf16.resize(bytes.size());

        simdutf::to_well_formed_utf16be(utf16_data, utf16_length, well_formed_utf16.data());
        utf16_data = well_formed_utf16.data();
    }

    auto utf8_length = simdutf::utf8_length_from_utf16be(utf16_data, utf16_length);

    String result;
    TRY(result.replace_with_new_string(utf8_length, [&](Bytes buffer) -> ErrorOr<void> {
        [[maybe_unused]] auto result = simdutf::convert_utf16be_to_utf8(utf16_data, utf16_length, reinterpret_cast<char*>(buffer.data()));
        ASSERT(result == buffer.size());
        return {};
    }));

    return result;
}

ErrorOr<String> String::from_stream(Stream& stream, size_t byte_count)
{
    String result;
    TRY(result.replace_with_new_string(byte_count, [&](Bytes buffer) -> ErrorOr<void> {
        TRY(stream.read_until_filled(buffer));
        if (!Utf8View { StringView { buffer } }.validate())
            return Error::from_string_literal("String::from_stream: Input was not valid UTF-8");
        return {};
    }));
    return result;
}

ErrorOr<String> String::from_string_builder(Badge<StringBuilder>, StringBuilder& builder)
{
    if (!Utf8View { builder.string_view() }.validate())
        return Error::from_string_literal("String::from_string_builder: Input was not valid UTF-8");

    String result;
    result.replace_with_string_builder(builder);
    return result;
}

String String::from_string_builder_without_validation(Badge<StringBuilder>, StringBuilder& builder)
{
    String result;
    result.replace_with_string_builder(builder);
    return result;
}

ErrorOr<String> String::repeated(u32 code_point, size_t count)
{
    VERIFY(is_unicode(code_point));

    Array<u8, 4> code_point_as_utf8;
    size_t i = 0;

    size_t code_point_byte_length = UnicodeUtils::code_point_to_utf8(code_point, [&](auto byte) {
        code_point_as_utf8[i++] = static_cast<u8>(byte);
    });

    auto total_byte_count = code_point_byte_length * count;

    String result;
    TRY(result.replace_with_new_string(total_byte_count, [&](Bytes buffer) {
        if (code_point_byte_length == 1) {
            buffer.fill(code_point_as_utf8[0]);
        } else {
            for (i = 0; i < count; ++i)
                memcpy(buffer.data() + (i * code_point_byte_length), code_point_as_utf8.data(), code_point_byte_length);
        }
        return ErrorOr<void> {};
    }));
    return result;
}

ErrorOr<String> String::vformatted(StringView fmtstr, TypeErasedFormatParams& params)
{
    StringBuilder builder;
    TRY(vformat(builder, fmtstr, params));
    return builder.to_string();
}

ErrorOr<Vector<String>> String::split(u32 separator, SplitBehavior split_behavior) const
{
    return split_limit(separator, 0, split_behavior);
}

ErrorOr<Vector<String>> String::split_limit(u32 separator, size_t limit, SplitBehavior split_behavior) const
{
    Vector<String> result;

    if (is_empty())
        return result;

    bool keep_empty = has_flag(split_behavior, SplitBehavior::KeepEmpty);

    size_t substring_start = 0;
    for (auto it = code_points().begin(); it != code_points().end() && (result.size() + 1) != limit; ++it) {
        u32 code_point = *it;
        if (code_point == separator) {
            size_t substring_length = code_points().iterator_offset(it) - substring_start;
            if (substring_length != 0 || keep_empty)
                TRY(result.try_append(TRY(substring_from_byte_offset_with_shared_superstring(substring_start, substring_length))));
            substring_start = code_points().iterator_offset(it) + it.underlying_code_point_length_in_bytes();
        }
    }
    size_t tail_length = code_points().byte_length() - substring_start;
    if (tail_length != 0 || keep_empty)
        TRY(result.try_append(TRY(substring_from_byte_offset_with_shared_superstring(substring_start, tail_length))));
    return result;
}

Optional<size_t> String::find_byte_offset(u32 code_point, size_t from_byte_offset) const
{
    auto code_points = this->code_points();
    if (from_byte_offset >= code_points.byte_length())
        return {};

    for (auto it = code_points.iterator_at_byte_offset(from_byte_offset); it != code_points.end(); ++it) {
        if (*it == code_point)
            return code_points.byte_offset_of(it);
    }

    return {};
}

Optional<size_t> String::find_byte_offset(StringView substring, size_t from_byte_offset) const
{
    auto view = bytes_as_string_view();
    if (from_byte_offset >= view.length())
        return {};

    auto index = memmem_optional(
        view.characters_without_null_termination() + from_byte_offset, view.length() - from_byte_offset,
        substring.characters_without_null_termination(), substring.length());

    if (index.has_value())
        return *index + from_byte_offset;
    return {};
}

bool String::operator==(FlyString const& other) const
{
    return static_cast<StringBase const&>(*this) == other.data({});
}

bool String::operator==(StringView other) const
{
    return bytes_as_string_view() == other;
}

ErrorOr<String> String::substring_from_byte_offset(size_t start, size_t byte_count) const
{
    if (!byte_count)
        return String {};
    return String::from_utf8(bytes_as_string_view().substring_view(start, byte_count));
}

ErrorOr<String> String::substring_from_byte_offset(size_t start) const
{
    VERIFY(start <= bytes_as_string_view().length());
    return substring_from_byte_offset(start, bytes_as_string_view().length() - start);
}

ErrorOr<String> String::substring_from_byte_offset_with_shared_superstring(size_t start, size_t byte_count) const
{
    return String { TRY(StringBase::substring_from_byte_offset_with_shared_superstring(start, byte_count)) };
}

ErrorOr<String> String::substring_from_byte_offset_with_shared_superstring(size_t start) const
{
    VERIFY(start <= bytes_as_string_view().length());
    return substring_from_byte_offset_with_shared_superstring(start, bytes_as_string_view().length() - start);
}

bool String::operator==(char const* c_string) const
{
    return bytes_as_string_view() == c_string;
}

u32 String::ascii_case_insensitive_hash() const
{
    return case_insensitive_string_hash(reinterpret_cast<char const*>(bytes().data()), bytes().size());
}

Utf8View String::code_points() const&
{
    return Utf8View(bytes_as_string_view());
}

ErrorOr<void> Formatter<String>::format(FormatBuilder& builder, String const& utf8_string)
{
    return Formatter<StringView>::format(builder, utf8_string.bytes_as_string_view());
}

ErrorOr<String> String::replace(StringView needle, StringView replacement, ReplaceMode replace_mode) const
{
    return StringUtils::replace(*this, needle, replacement, replace_mode);
}

ErrorOr<String> String::reverse() const
{
    // FIXME: This handles multi-byte code points, but not e.g. grapheme clusters.
    // FIXME: We could avoid allocating a temporary vector if Utf8View supports reverse iteration.
    auto code_point_length = code_points().length();

    Vector<u32> code_points;
    TRY(code_points.try_ensure_capacity(code_point_length));

    for (auto code_point : this->code_points())
        code_points.unchecked_append(code_point);

    auto builder = TRY(StringBuilder::create(code_point_length * sizeof(u32)));
    while (!code_points.is_empty())
        TRY(builder.try_append_code_point(code_points.take_last()));

    return builder.to_string();
}

ErrorOr<String> String::trim(Utf8View const& code_points_to_trim, TrimMode mode) const
{
    auto trimmed = code_points().trim(code_points_to_trim, mode);
    return String::from_utf8(trimmed.as_string());
}

ErrorOr<String> String::trim(StringView code_points_to_trim, TrimMode mode) const
{
    return trim(Utf8View { code_points_to_trim }, mode);
}

ErrorOr<String> String::trim_ascii_whitespace(TrimMode mode) const
{
    return trim(" \n\t\v\f\r"sv, mode);
}

bool String::contains(StringView needle, CaseSensitivity case_sensitivity) const
{
    return StringUtils::contains(bytes_as_string_view(), needle, case_sensitivity);
}

bool String::contains(u32 needle, CaseSensitivity case_sensitivity) const
{
    auto needle_as_string = String::from_code_point(needle);
    return contains(needle_as_string.bytes_as_string_view(), case_sensitivity);
}

bool String::starts_with(u32 code_point) const
{
    if (is_empty())
        return false;

    return *code_points().begin() == code_point;
}

bool String::starts_with_bytes(StringView bytes, CaseSensitivity case_sensitivity) const
{
    return bytes_as_string_view().starts_with(bytes, case_sensitivity);
}

bool String::ends_with(u32 code_point) const
{
    ASSERT(is_unicode(code_point));

    if (is_empty())
        return false;

    Array<u8, 4> code_point_as_utf8;
    size_t i = 0;

    size_t code_point_byte_length = UnicodeUtils::code_point_to_utf8(code_point, [&](auto byte) {
        code_point_as_utf8[i++] = static_cast<u8>(byte);
    });

    return ends_with_bytes(StringView { code_point_as_utf8.data(), code_point_byte_length });
}

bool String::ends_with_bytes(StringView bytes, CaseSensitivity case_sensitivity) const
{
    return bytes_as_string_view().ends_with(bytes, case_sensitivity);
}

unsigned Traits<String>::hash(String const& string)
{
    return string.hash();
}

ByteString String::to_byte_string() const
{
    return ByteString(bytes_as_string_view());
}

ErrorOr<String> String::from_byte_string(ByteString const& byte_string)
{
    return String::from_utf8(byte_string.view());
}

String String::to_ascii_lowercase() const
{
    if (!any_of(bytes(), is_ascii_upper_alpha))
        return *this;

    String result;

    MUST(result.replace_with_new_string(byte_count(), [&](Bytes buffer) -> ErrorOr<void> {
        for (auto [i, byte] : enumerate(bytes()))
            buffer[i] = static_cast<u8>(AK::to_ascii_lowercase(byte));
        return {};
    }));

    return result;
}

String String::to_ascii_uppercase() const
{
    if (!any_of(bytes(), is_ascii_lower_alpha))
        return *this;

    String result;

    MUST(result.replace_with_new_string(byte_count(), [&](Bytes buffer) -> ErrorOr<void> {
        for (auto [i, byte] : enumerate(bytes()))
            buffer[i] = static_cast<u8>(AK::to_ascii_uppercase(byte));
        return {};
    }));

    return result;
}

bool String::equals_ignoring_ascii_case(String const& other) const
{
    return StringUtils::equals_ignoring_ascii_case(bytes_as_string_view(), other.bytes_as_string_view());
}

bool String::equals_ignoring_ascii_case(StringView other) const
{
    return StringUtils::equals_ignoring_ascii_case(bytes_as_string_view(), other);
}

ErrorOr<String> String::repeated(String const& input, size_t count)
{
    if (Checked<u32>::multiplication_would_overflow(count, input.bytes().size()))
        return Error::from_errno(EOVERFLOW);

    String result;
    size_t input_size = input.bytes().size();
    TRY(result.replace_with_new_string(count * input_size, [&](Bytes buffer) {
        if (input_size == 1) {
            buffer.fill(input.bytes().first());
        } else {
            for (size_t i = 0; i < count; ++i)
                input.bytes().copy_to(buffer.slice(i * input_size, input_size));
        }
        return ErrorOr<void> {};
    }));

    return result;
}

String String::bijective_base_from(size_t value, Case target_case, unsigned base, StringView map)
{
    value++;
    if (map.is_null())
        map = target_case == Case::Upper ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ"sv : "abcdefghijklmnopqrstuvwxyz"sv;

    VERIFY(base >= 2 && base <= map.length());

    // The '8 bits per byte' assumption may need to go?
    Array<char, round_up_to_power_of_two(sizeof(size_t) * 8 + 1, 2)> buffer;
    size_t i = 0;
    do {
        auto remainder = value % base;
        auto new_value = value / base;
        if (remainder == 0) {
            new_value--;
            remainder = map.length();
        }

        buffer[i++] = map[remainder - 1];
        value = new_value;
    } while (value > 0);

    for (size_t j = 0; j < i / 2; ++j)
        swap(buffer[j], buffer[i - j - 1]);

    return MUST(from_utf8(ReadonlyBytes(buffer.data(), i)));
}

String String::greek_letter_from(size_t value)
{
    static StringView const map = "αβγδεζηθικλμνξοπρστυφχψω"sv;
    static unsigned const base = 24;

    StringBuilder builder;
    while (value > 0) {
        value--;
        builder.append(map.substring_view((value % base) * 2, 2));
        value /= base;
    }

    return MUST(builder.to_string_without_validation().reverse());
}

String String::roman_number_from(size_t value, Case target_case)
{
    if (value > 3999)
        return number(value);

    StringBuilder builder;

    while (value > 0) {
        if (value >= 1000) {
            builder.append(target_case == Case::Upper ? 'M' : 'm');
            value -= 1000;
        } else if (value >= 900) {
            builder.append(target_case == Case::Upper ? "CM"sv : "cm"sv);
            value -= 900;
        } else if (value >= 500) {
            builder.append(target_case == Case::Upper ? 'D' : 'd');
            value -= 500;
        } else if (value >= 400) {
            builder.append(target_case == Case::Upper ? "CD"sv : "cd"sv);
            value -= 400;
        } else if (value >= 100) {
            builder.append(target_case == Case::Upper ? 'C' : 'c');
            value -= 100;
        } else if (value >= 90) {
            builder.append(target_case == Case::Upper ? "XC"sv : "xc"sv);
            value -= 90;
        } else if (value >= 50) {
            builder.append(target_case == Case::Upper ? 'L' : 'l');
            value -= 50;
        } else if (value >= 40) {
            builder.append(target_case == Case::Upper ? "XL"sv : "xl"sv);
            value -= 40;
        } else if (value >= 10) {
            builder.append(target_case == Case::Upper ? 'X' : 'x');
            value -= 10;
        } else if (value == 9) {
            builder.append(target_case == Case::Upper ? "IX"sv : "ix"sv);
            value -= 9;
        } else if (value >= 5 && value <= 8) {
            builder.append(target_case == Case::Upper ? 'V' : 'v');
            value -= 5;
        } else if (value == 4) {
            builder.append(target_case == Case::Upper ? "IV"sv : "iv"sv);
            value -= 4;
        } else if (value <= 3) {
            builder.append(target_case == Case::Upper ? 'I' : 'i');
            value -= 1;
        }
    }

    return builder.to_string_without_validation();
}

template<Integral T>
String String::number(T value)
{
    // Maximum number of base-10 digits for T + sign
    constexpr size_t max_digits = sizeof(T) * 3 + 2;
    char buffer[max_digits];
    char* ptr = buffer + max_digits;
    bool is_negative = false;

    using UnsignedT = MakeUnsigned<T>;

    UnsignedT unsigned_value;
    if constexpr (IsSigned<T>) {
        if (value < 0) {
            is_negative = true;
            // Handle signed min correctly
            unsigned_value = static_cast<UnsignedT>(0) - static_cast<UnsignedT>(value);
        } else {
            unsigned_value = static_cast<UnsignedT>(value);
        }
    } else {
        unsigned_value = value;
    }

    if (unsigned_value == 0) {
        *--ptr = '0';
    } else {
        while (unsigned_value != 0) {
            *--ptr = '0' + (unsigned_value % 10);
            unsigned_value /= 10;
        }
    }

    if (is_negative) {
        *--ptr = '-';
    }

    size_t size = buffer + max_digits - ptr;
    return from_utf8_without_validation(ReadonlyBytes { ptr, size });
}

template String String::number(char);
template String String::number(signed char);
template String String::number(unsigned char);
template String String::number(signed short);
template String String::number(unsigned short);
template String String::number(int);
template String String::number(unsigned int);
template String String::number(long);
template String String::number(unsigned long);
template String String::number(long long);
template String String::number(unsigned long long);

}
