// The owner's food lists (ADR-010): dishes never wanted, dishes eaten but
// not liked, and liked dishes (ADR-010 amendment), from menu-lists.txt in
// the settings folder:
//
//   # comment
//   [istemiyorum]
//   kuru fasulye
//   [sevmiyorum]
//   bamya
//   [seviyorum]
//   kebap
//
// ("[never]", "[dislike]" and "[like]" are accepted too.) A dish matches a name when
// the name occurs in it, ignoring case by Turkish rules (İ/i, I/ı) and
// extra spaces. Platform independent and tested.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "MenuData.h"

namespace stm32_audio::host {

enum class DishMark {
    None,
    Dislike,  // eats it, does not like it
    Never,    // does not want it at all
    Liked,    // likes it (a dish on two lists takes the first of never,
              // dislike, liked)
};

struct MenuLists {
    std::vector<std::string> never;    // lower case (turkishLower)
    std::vector<std::string> dislike;
    std::vector<std::string> liked;
};

MenuLists parseMenuLists(std::string_view text);

// @p text in lower case by Turkish rules (A-Z, İ -> i, I -> ı, Ç Ğ Ö Ş Ü),
// whitespace runs folded to one space, trimmed.
std::string turkishLower(std::string_view text);

DishMark markOf(const std::string& dish, const MenuLists& lists);

// The worst mark of the day's five dishes (main, side, soup, dessert,
// extra; not the salad bar): Never, Dislike or None (Liked is not "worse").
DishMark markOf(const MenuDay& day, const MenuLists& lists);

// The dishes of @p day with the mark @p mark, in the order of the day.
std::vector<std::string> dishesMarked(const MenuDay& day, const MenuLists& lists, DishMark mark);

// The dessert without the fruit option (#49): "Meyve / Fıstıklı baklava"
// -> "Fıstıklı baklava"; "Meyve" alone -> "". The parts are separated by
// "/"; a part that is fruit ("meyve", "mevsim meyvesi", ...) is left out.
std::string withoutFruit(const std::string& dessert);

// The text a new menu-lists.txt starts with.
extern const char* const kMenuListsTemplate;

}  // namespace stm32_audio::host
