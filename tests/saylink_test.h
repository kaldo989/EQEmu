/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "common/emu_constants.h"
#include "common/say_link.h"
#include "cppunit/cpptest.h"

class SaylinkTest : public Test::Suite {
	typedef void(SaylinkTest::*TestFunction)(void);
public:
	SaylinkTest() {
		TEST_ADD(SaylinkTest::EtagMustBeADigitTest);
		TEST_ADD(SaylinkTest::DetectLinkDataWidthTest);
		TEST_ADD(SaylinkTest::GenerateLinkBodyClampsEtagTest);
		TEST_ADD(SaylinkTest::DegenerateLinkBodyFromMessageTest);
	}

	~SaylinkTest() {
	}

	private:
	void EtagMustBeADigitTest() {
		// The client's TagSizes table only covers '0' - '9'. Hex letters are valid in a link body but
		// not as an etag, so they have to be rejected.
		for (char c = '0'; c <= '9'; ++c) {
			TEST_ASSERT(EQ::saylink::IsLinkEtag(c));
		}

		TEST_ASSERT(!EQ::saylink::IsLinkEtag('A'));
		TEST_ASSERT(!EQ::saylink::IsLinkEtag('F'));
		TEST_ASSERT(!EQ::saylink::IsLinkEtag(' '));
		TEST_ASSERT(!EQ::saylink::IsLinkEtag('\x12'));
	}

	void DetectLinkDataWidthTest() {
		// A canonical block followed by display text that starts with a hex character. Measuring the hex run
		// would report 56 here, which matches no layout, so the fixed width test is what identifies it.
		const std::string canonical_hex_text = "0" + std::string(55, 'A') + "Corpses";
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth(canonical_hex_text), 55);

		// A full Laurion block is unambiguous even when the display text is hex as well.
		const std::string laurion_hex_text = "0" + std::string(90, 'A') + "Doors";
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth(laurion_hex_text), 90);

		// A segment that does not fill either width is not a parseable link.
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth("0" + std::string(54, 'A') + "text"), 0);
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth("0" + std::string(55, 'A')), 55);

		// Non digit etag and empty segment still mean not a link.
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth("A" + std::string(90, 'A')), 0);
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth(" " + std::string(90, 'A')), 0);
		TEST_ASSERT_EQUALS(EQ::saylink::DetectLinkDataWidth(""), 0);
	}

	void GenerateLinkBodyClampsEtagTest() {
		EQ::SayLinkBody_Struct body = {};
		body.action_id = 0x0A;   // would render as 'A' without the clamp

		std::string out;
		TEST_ASSERT(EQ::saylink::GenerateLinkBody(out, body, EQ::versions::ClientVersion::RoF2));
		TEST_ASSERT_EQUALS(out.length(), EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::RoF2));
		TEST_ASSERT_EQUALS(out[0], '0');

		TEST_ASSERT(EQ::saylink::GenerateLinkBody(out, body, EQ::versions::ClientVersion::Laurion));
		TEST_ASSERT_EQUALS(out.length(), EQ::constants::GetSayLinkBodySize(EQ::versions::ClientVersion::Laurion));
		TEST_ASSERT_EQUALS(out[0], '0');

		// A digit etag is passed through unchanged.
		body.action_id = 0x03;
		TEST_ASSERT(EQ::saylink::GenerateLinkBody(out, body, EQ::versions::ClientVersion::RoF2));
		TEST_ASSERT_EQUALS(out[0], '3');
	}

	void DegenerateLinkBodyFromMessageTest() {
		EQ::SayLinkBody_Struct body = {};

		// A link opened by a non digit etag is not a link, so it must not be parsed.
		const std::string message = "prefix" "\x12" "A" + std::string(90, 'A') + "text" "\x12" "suffix";
		TEST_ASSERT(!EQ::saylink::DegenerateLinkBodyFromMessage(body, message, 6));

		// The same segment with a digit etag parses.
		const std::string valid = "prefix" "\x12" "0" + std::string(90, 'A') + "text" "\x12" "suffix";
		TEST_ASSERT(EQ::saylink::DegenerateLinkBodyFromMessage(body, valid, 6));
		TEST_ASSERT_EQUALS(body.action_id, 0);
	}
};
