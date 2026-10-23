//
//  ZEPTO-8 — Fantasy console emulator
//
//  Copyright © 2016—2020 Sam Hocevar <sam@hocevar.net>
//
//  This program is free software. It comes without any warranty, to
//  the extent permitted by applicable law. You can redistribute it
//  and/or modify it under the terms of the Do What the Fuck You Want
//  to Public License, Version 2, as published by the WTFPL Task Force.
//  See http://www.wtfpl.net/ for more details.
//
//  PBC+ port: the std::regex / std::map / codecvt machinery is replaced by
//  a plain table walk. The regex alone took more RAM than the device has
//  to spare.

#include <cstdint>
#include <string>
#include <cstring>
#include <string_view>

#include "emojiconversion.h"

std::string_view charset::to_utf8[256];

static bool tables_ready;

static void init_tables()
{
    // The complete PICO-8 charmap, from 0 to 255. We cannot just store
    // codepoints because some emoji glyphs are combinations of several
    // codepoints, e.g. ⬇️ is U+2B07 (down arrow) + U+FE0F (variation
    // selector-16).
    static char const utf8_chars[] =
        "\0¹²³⁴⁵⁶⁷⁸\t\nᵇᶜ\rᵉᶠ▮■□⁙⁘‖◀▶「」¥•、。゛゜"
        " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNO"
        "PQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~○"
        "█▒🐱⬇️░✽●♥☉웃⌂⬅️😐♪🅾️◆…➡️★⧗⬆️ˇ∧❎▤▥あいうえおか"
        "きくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよ"
        "らりるれろわをんっゃゅょアイウエオカキクケコサシスセソタチツテト"
        "ナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲンッャュョ◜◝";

    char const *p8 = utf8_chars;
    for (int i = 0; i < 256; ++i)
    {
        uint8_t c = (uint8_t)*p8;
        size_t len8 = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if ((uint8_t)p8[len8] == 0xef && (uint8_t)p8[len8 + 1] == 0xb8
            && (uint8_t)p8[len8 + 2] == 0x8f)
            len8 += 3;          // followed by U+FE0F
        charset::to_utf8[i] = std::string_view(p8, len8);
        p8 += len8;
    }
    tables_ready = true;
}

std::string charset::utf8_to_pico8(std::string const &str)
{
    if (!tables_ready)
        init_tables();

    std::string ret;
    ret.reserve(str.size());
    const char *p = str.data(), *end = p + str.size();

    while (p < end)
    {
        if ((uint8_t)*p < 0x80)
        {
            ret += *p++;
            continue;
        }
        // longest table entry that matches here
        int best = -1;
        size_t bestlen = 0;
        for (int i = 1; i < 256; ++i)
        {
            size_t n = charset::to_utf8[i].size();
            if (n > 1 && n > bestlen && (size_t)(end - p) >= n
                && memcmp(p, charset::to_utf8[i].data(), n) == 0)
            {
                best = i;
                bestlen = n;
            }
        }
        if (best >= 0)
        {
            ret += (char)best;
            p += bestlen;
        }
        else
        {
            ret += *p++;
        }
    }

    return ret;
}

std::string charset::pico8_to_utf8(std::string const &str)
{
    if (!tables_ready)
        init_tables();

    std::string ret;
    for (uint8_t ch : str)
        ret += std::string(to_utf8[ch]);
    return ret;
}



std::string charset::upper_to_emoji(std::string str)
{
	static const char *const glyphs[26] = {
		"█","▒","🐱","⬇️","░","✽","●","♥","☉","웃","⌂","⬅️","😐",
		"♪","🅾️","◆","…","➡️","★","⧗","⬆️","ˇ","∧","❎","▤","▥"
	};

	std::string newstring = "";

	for(unsigned i = 0; i < str.length(); i++){
		char c = str.at(i);
		if (c >= 'A' && c <= 'Z') {
			newstring += glyphs[c - 'A'];
		} else {
			newstring += c;
		}
	}

    return charset::utf8_to_pico8(newstring);
}
