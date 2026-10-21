// Text of a Windows toast notification (spike #33). A toast's payload is
// XML ("<toast><visual><binding template=...><text>Title</text>
// <text>Body</text>..."); the texts are what the board would show.
// Platform independent and tested; the Windows probe reads the payloads.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stm32_audio::host {

// The content of every <text> element of @p xml in order, XML entities
// decoded (&amp; &lt; &gt; &quot; &apos; &#NN; &#xNN;), whitespace runs
// folded to one space and trimmed; empty texts are skipped.
std::vector<std::string> toastTexts(std::string_view xml);

// A Windows FILETIME (100 ns since 1601-01-01 UTC) as seconds since
// 1970-01-01 UTC; 0 for a time before 1970.
std::int64_t unixFromFiletime(std::int64_t filetime);

}  // namespace stm32_audio::host
