// Menu import from saved portal pages: the cafeteria's "Aylık Yemek
// Menüsü" page, saved in the browser (Ctrl+S, .html) once on the "Öğle"
// tab and once on the "Salatabar" tab. The page is read without the
// Claude API: one column per day,
//
//   <div class="portal-food-menu-table-column"><h1>05 Ekim<br>Pazartesi</h1>
//     <div>KAĞIT KEBABI</div> ... five lines (or about six salad bar items)
//   </div>
//
// with <div class="empty"> on a day without a menu and "RESMİ TATİL" on a
// holiday. The days are merged into the month files like the image import
// (MenuImport.h). Platform independent and tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "MenuImport.h"

namespace stm32_audio::host {

inline constexpr std::size_t kMaxMenuPageBytes = 4U * 1024U * 1024U;

// True for a ".html" or ".htm" file name.
bool isMenuPageName(const std::string& fileName);

enum class MenuPageKind {
    Meal,      // "Öğle": main, side, soup, dessert, extra
    SaladBar,  // "Salatabar"
};

struct MenuPage {
    MenuPageKind kind = MenuPageKind::Meal;
    std::vector<DatedMenuDay> days;  // days without a menu are left out
    std::string error;               // empty on success
};

// Read one saved page. The kind is the page's active tab; other tabs
// ("Akşam", "Diyet", ...) are an error. Days get the year closest to the
// import date (menuYearFor()).
MenuPage parseMenuPage(const std::string& html, int importYear, int importMonth, std::uint8_t language);

// "MEYVE/FISTIKLI BAKLAVA" -> "Meyve / fıstıklı baklava": lower case by
// Turkish rules, the first letter capital, spaces around "/", as the image
// import writes it.
std::string menuSentenceCase(std::string_view text);

struct MenuPageFile {
    std::string name;  // for messages
    std::string html;
};

// The whole import: every page is read first; nothing is written when one
// of them is not a menu page.
MenuImportResult importMenuPages(const std::vector<MenuPageFile>& pages, IMenuFiles& files, int importYear,
                                 int importMonth, std::uint8_t language);

}  // namespace stm32_audio::host
