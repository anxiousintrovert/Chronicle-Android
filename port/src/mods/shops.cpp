#include "shops.hpp"

#include <algorithm>
#include <array>
#include <map>

#include "itemdata.hpp"
#include "shop.hpp"

// Native public symbol; identical definition to shop.cpp, kept outside ps2/.
struct ITEM_MONEY_ENTRY {
    s16 buy_price;
    s16 sell_price;
};

extern ITEM_MONEY_ENTRY PriceList[296];

namespace mods {
namespace {
using Json = nlohmann::json;
using Stock = std::array<s16, 20>;
using Price = std::array<s16, 2>;

template <class T>
struct Claim {
    std::string owner;
    T           original{}, applied{};
};

struct Claims {
    std::map<int, Claim<Stock>> stocks;
    std::map<int, Claim<Price>> prices;
} owners;

Stock StockAt(int shop) {
    Stock result;
    std::copy_n(GetItemShopList(shop), result.size(), result.begin());
    return result;
}

Price PriceAt(int item) {
    const auto &p = PriceList[item - ITEM_ATTACH_START];
    return {p.buy_price, p.sell_price};
}

void WriteStock(int shop, const Stock &value) noexcept { std::copy(value.begin(), value.end(), GetItemShopList(shop)); }

void WritePrice(int item, const Price &value) noexcept { PriceList[item - ITEM_ATTACH_START] = {value[0], value[1]}; }

bool ValidItem(int item) {
    if (item >= ITEM_WEAPON_START && item < ITEM_WEAPON_END) {
        return GetWeaponData(item) != nullptr;
    }
    if (item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START) {
        return GetItemData(item) != nullptr;
    }
    return item >= ITEM_ATTACH_START && item <= 130 && GetAttachData(item) != nullptr;
}

template <class T>
void CheckOwner(const std::map<int, Claim<T>> &claims, int id, const std::string &owner) {
    if (owner.empty()) {
        throw std::runtime_error("shop writes require a runtime package owner");
    }
    const auto found = claims.find(id);
    if (found != claims.end() && found->second.owner != owner) {
        throw std::runtime_error("shop resource is already owned by " + found->second.owner);
    }
}

template <class T>
void Set(std::map<int, Claim<T>> &claims, int id, const std::string &owner, const T &current, const T &value) {
    CheckOwner(claims, id, owner);
    auto &claim = claims[id];
    if (claim.owner.empty() || claim.applied != current) {
        claim.original = current;
    }
    claim.owner = owner;
    claim.applied = value;
}

template <class T>
T Reset(std::map<int, Claim<T>> &claims, int id, const std::string &owner, T current) {
    CheckOwner(claims, id, owner);
    const auto found = claims.find(id);
    if (found != claims.end()) {
        if (current == found->second.applied) {
            current = found->second.original;
        }
        claims.erase(found);
    }
    return current;
}

Json Shop(int id) {
    Json       items = Json::array();
    const auto stock = StockAt(id);
    for (int slot = 0; slot < 20; ++slot) {
        const int item = stock[slot];
        if (item < ITEM_ATTACH_START || item >= ITEM_WEAPON_END) {
            continue;
        }
        const auto price = PriceAt(item);
        const auto found = owners.prices.find(item);
        items.push_back({
            {"slot",        slot                                                   },
            {"item",        item                                                   },
            {"buy",         price[0]                                               },
            {"sell",        price[1]                                               },
            {"price_owner", found == owners.prices.end() ? "" : found->second.owner}
        });
    }
    const auto found = owners.stocks.find(id);
    return {
        {"shop",     id                                                     },
        {"currency", "gilda"                                                },
        {"capacity", 20                                                     },
        {"items",    items                                                  },
        {"owner",    found == owners.stocks.end() ? "" : found->second.owner}
    };
}
} // namespace

bool IsShopWrite(std::string_view op) {
    return op == "shops.set_items" || op == "shops.register_item" || op == "shops.remove_item" ||
           op == "shops.reset_items" || op == "shops.set_price" || op == "shops.reset_price";
}

std::string ValidateShop(std::string_view op, const Json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    std::map<std::string, std::pair<int, int>> fields;
    if (op != "shops.list") {
        if (op == "shops.set_price" || op == "shops.reset_price") {
            fields["item"] = {81, 376};
        } else {
            fields["shop"] = {0, 17};
        }
    }
    if (op == "shops.register_item" || op == "shops.remove_item") {
        fields["item"] = {81, 376};
    }
    if (op == "shops.set_price") {
        fields["buy"] = {0, 32767};
        fields["sell"] = {0, 32767};
    }
    for (const auto &[key, value] : a.items()) {
        if (key == "items" && op == "shops.set_items") {
            if (!value.is_array() || value.size() > 20) {
                return "items must be an array of at most 20 retail IDs";
            }
            for (const auto &item : value) {
                if (!item.is_number_integer() || item.get<double>() < 81 || item.get<double>() > 376 ||
                    !ValidItem(item.get<int>()) || item.get<int>() == ITEM_ATTACH_SYNTHESIS_SPHERE) {
                    return "invalid retail shop stock item";
                }
            }
            continue;
        }
        const auto field = fields.find(key);
        if (field == fields.end() || !value.is_number_integer() || value.get<double>() < field->second.first || value.get<double>() > field->second.second) {
            return "invalid shop argument: " + key;
        }
        if (key == "item" && (!ValidItem(value.get<int>()) ||
                              (op == "shops.register_item" && value.get<int>() == ITEM_ATTACH_SYNTHESIS_SPHERE))) {
            return "invalid retail shop item";
        }
    }
    for (const auto &[key, range] : fields) {
        if (!a.contains(key)) {
            return "missing shop argument: " + key;
        }
    }
    if (op == "shops.set_items" && !a.contains("items")) {
        return "missing shop argument: items";
    }
    return {};
}

Json ReadShop(std::string_view op, const Json &a) {
    const auto error = ValidateShop(op, a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    if (op == "shops.get") {
        return Shop(a.at("shop"));
    }
    if (op != "shops.list") {
        throw std::runtime_error("shop operation is not a query");
    }
    Json result = Json::array();
    for (int i = 0; i < 18; ++i) {
        result.push_back(Shop(i));
    }
    return {
        {"shops", result}
    };
}

bool HasOwnedShops() { return !owners.stocks.empty() || !owners.prices.empty(); }

void RetainOwnedShops(std::span<const std::string> active) {
    for (auto it = owners.stocks.begin(); it != owners.stocks.end();) {
        if (std::ranges::find(active, it->second.owner) != active.end()) {
            ++it;
            continue;
        }
        if (StockAt(it->first) == it->second.applied) {
            WriteStock(it->first, it->second.original);
        }
        it = owners.stocks.erase(it);
    }
    for (auto it = owners.prices.begin(); it != owners.prices.end();) {
        if (std::ranges::find(active, it->second.owner) != active.end()) {
            ++it;
            continue;
        }
        if (PriceAt(it->first) == it->second.applied) {
            WritePrice(it->first, it->second.original);
        }
        it = owners.prices.erase(it);
    }
}

struct ShopTransaction::Impl {
    Claims               claims = owners;
    std::map<int, Stock> stocks;
    std::map<int, Price> prices;
};

ShopTransaction::ShopTransaction() = default;
ShopTransaction::~ShopTransaction() = default;

bool ShopTransaction::Stage(const std::vector<GameCommand> &commands, bool writable, std::string &error) {
    staged_.reset();
    try {
        if (commands.size() > 64) {
            throw std::runtime_error("callback write budget exceeded");
        }
        for (const auto &command : commands) {
            const auto &op = command.operation;
            if (!IsShopWrite(op)) {
                continue;
            }
            if (!writable) {
                throw std::runtime_error("shop writes require stable play outside town menus and transitions");
            }
            const auto &a = command.arguments;
            error = ValidateShop(op, a);
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            if (!staged_) {
                staged_ = std::make_unique<Impl>();
            }
            auto &state = *staged_;
            if (op == "shops.set_price" || op == "shops.reset_price") {
                const int id = a.at("item");
                auto [it, inserted] = state.prices.try_emplace(id, PriceAt(id));
                if (op == "shops.reset_price") {
                    it->second = Reset(state.claims.prices, id, command.owner, it->second);
                } else {
                    const Price value{a.at("buy").get<s16>(), a.at("sell").get<s16>()};
                    Set(state.claims.prices, id, command.owner, it->second, value);
                    it->second = value;
                }
            } else {
                const int id = a.at("shop");
                auto [it, inserted] = state.stocks.try_emplace(id, StockAt(id));
                if (op == "shops.reset_items") {
                    it->second = Reset(state.claims.stocks, id, command.owner, it->second);
                    continue;
                }
                CheckOwner(state.claims.stocks, id, command.owner);
                std::vector<s16> items;
                if (op == "shops.set_items") {
                    items = a.at("items").get<std::vector<s16>>();
                } else {
                    for (const int item : it->second) {
                        if (item >= ITEM_ATTACH_START) {
                            items.push_back(item);
                        }
                    }
                    if (op == "shops.register_item") {
                        items.push_back(a.at("item").get<s16>());
                    } else {
                        const auto found = std::ranges::find(items, a.at("item").get<s16>());
                        if (found == items.end()) {
                            throw std::runtime_error("item is not stocked by this shop");
                        }
                        items.erase(found);
                    }
                }
                if (items.size() > 20) {
                    throw std::runtime_error("native shop capacity is 20 goods");
                }
                for (const int item : items) {
                    if (!ValidItem(item) || item == ITEM_ATTACH_SYNTHESIS_SPHERE) {
                        throw std::runtime_error("invalid retail shop stock item");
                    }
                }
                Stock value;
                value.fill(-1);
                std::copy(items.begin(), items.end(), value.begin());
                Set(state.claims.stocks, id, command.owner, it->second, value);
                it->second = value;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        staged_.reset();
        return false;
    }
}

void ShopTransaction::Commit() noexcept {
    if (!staged_) {
        return;
    }
    for (const auto &[id, value] : staged_->stocks) {
        WriteStock(id, value);
    }
    for (const auto &[id, value] : staged_->prices) {
        WritePrice(id, value);
    }
    owners.stocks.swap(staged_->claims.stocks);
    owners.prices.swap(staged_->claims.prices);
    staged_.reset();
}
} // namespace mods
