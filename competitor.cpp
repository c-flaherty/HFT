#include "kirin.hpp"
#include <cassert>
#include <iostream>
#include <iomanip>
#include <fstream>

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_map>
#include <unordered_set>


struct LimitOrder {
  price_t price;
  mutable quantity_t quantity; // mutable so set doesn't complain
  order_id_t order_id;
  long long time;
  trader_id_t trader_id;
  bool buy;

  bool operator <(const LimitOrder& other) const {
    // < means more aggressive
    if (buy) {
      return price > other.price || (price == other.price && time < other.time);
    } else {
      return price < other.price || (price == other.price && time < other.time);
    }
  }

  bool trades_with(const LimitOrder& other) const {
    return ((buy && !other.buy && price >= other.price) ||
            (!buy && other.buy && price <= other.price));
  }
};


struct MyBook {
public:

  MyBook() {}

  price_t get_bbo(bool buy) const {
    const std::set<LimitOrder>& side = sides[buy];

    if (side.empty()) {
      return 0.0;
    }
    return side.begin()->price;
  }

  price_t get_2nd_bbo(bool buy) const {
    const std::set<LimitOrder>&  side = sides[buy];

    if (side.empty()) {
      return 0.0;
    }

    std::set<LimitOrder>::iterator s = sides[buy].begin();
    s++;
    return (s->price);
  }

  /* ---USE THIS FUNCTION TO GENERATE A SIGNAL---
  Currently, I calculate bid and ask volume for all levels.
  I then calculate (bid_vol - ask_vol)/(bid_vol + ask_vol)
  */
  double get_signal(int num_levels) const {
    bool bid = true, ask = false;
    price_t best_bid = get_bbo(bid);
    price_t best_offer = get_bbo(ask);

    double signal = 0.0;

    if (best_bid == 0.0 || best_offer == 0.0) {
      return signal; // no signal can be found in this case
    }

    double weight;
    // Calculate bid volume for up to n levels
    quantity_t bid_volume = 0;
    std::set<LimitOrder>::iterator bid_level=sides[1].begin();
    for (int i = 0; i<num_levels && bid_level!=sides[1].end(); i++) {
      if (bid_level -> quantity > 5000) {
        continue;
      }
      weight = 1 - abs(best_bid - bid_level->price)/best_bid;
      bid_volume += weight * (bid_level->quantity);
      bid_level++;
    }

    // Calculate ask volume for up to n levels
    quantity_t ask_volume = 0;
    std::set<LimitOrder>::iterator ask_level=sides[0].begin();
    for (int i = 0; i<num_levels && ask_level!=sides[0].end(); i++) {
      if (ask_level -> quantity > 5000) {
        continue;
      }
      weight = 1 - abs(ask_level->price - best_offer)/best_offer;
      ask_volume += weight * (ask_level->quantity);
      ask_level++;
    }

    // Calculate signal
    signal = ((double)bid_volume - (double)ask_volume)/((double)bid_volume + (double)ask_volume);

    return signal;
  }

  price_t get_mid_price(price_t default_to) const {
    price_t best_bid = get_bbo(true);
    price_t best_offer = get_bbo(false);

    if (best_bid == 0.0 || best_offer == 0.0) {
      return default_to;
    }

    return 0.5 * (best_bid + best_offer);

  }


  void insert(Common::Order order_to_insert) {

    LimitOrder order_left = {
      .price = order_to_insert.price,
      .quantity = order_to_insert.quantity,
      .order_id = order_to_insert.order_id,
      .time = std::chrono::steady_clock::now().time_since_epoch().count(),
      .trader_id = order_to_insert.trader_id,
      .buy = order_to_insert.buy
    };

    auto& side = sides[(size_t)order_left.buy];

    auto it_new = side.insert(order_left);
    assert(it_new.second);
    order_map[order_left.order_id] = it_new.first;

  }

  void cancel(trader_id_t trader_id, order_id_t order_id) {


    if (!order_map.count(order_id)) {
      std::cout << "order " << order_id << " nonexistent" << std::endl;
      return;
    }
    auto it = order_map[order_id];

    order_map.erase(order_id);

    auto& side = sides[(size_t)it->buy];
    side.erase(it);
  }

  quantity_t decrease_qty(order_id_t order_id, quantity_t decrease_by) {

    if (!order_map.count(order_id)) {
      return -1;
    }

    std::set<LimitOrder>::iterator it = order_map[order_id];

    if (decrease_by >= it->quantity) {
      order_map.erase(order_id);
      std::set<LimitOrder>& side = sides[(size_t)it->buy];
      side.erase(it);
      return 0;

    } else {

      it->quantity -= decrease_by;
      return it->quantity;
    }

  }

  void print_book(std::string fp, const std::unordered_map<order_id_t, Common::Order>& mine={}) {
    if (fp == "") {
      return;
    }

    std::ofstream fout(fp, std::fstream::app);

    fout << "offers\n";
    for (auto rit = sides[0].rbegin(); rit != sides[0].rend(); rit++) {
      auto x = *rit;
      fout << x.price << ' ' << x.quantity;
      if (mine.count(x.order_id)) {
        fout << " (mine)";
      }
      fout << '\n';
    }

    fout << "\nbids\n";

    for (auto& x : sides[1]) {
      fout << x.price << ' ' << x.quantity;
      if (mine.count(x.order_id)) {
        fout << " (mine)";
      }
      fout << '\n';
    }

    fout << "EOF" << std::endl;


    fout.close();
  }

  quantity_t quote_size(bool buy) {
    price_t p = get_bbo(buy);
    if (p == 0.0) {
      return 0;
    }

    quantity_t ans = 0;
    for (auto& x : sides[buy]) {
      if (x.price != p) {
        break;
      }
      ans += x.quantity;
    }
    return ans;
  }
  price_t spread() {

    price_t best_bid = get_bbo(true);
    price_t best_offer = get_bbo(false);

    if (best_bid == 0.0 || best_offer == 0.0) {
      return 0.0;
    }

    return best_offer - best_bid;
  }

private:
  std::set<LimitOrder> sides[2];
  std::unordered_map<order_id_t, std::set<LimitOrder>::iterator> order_map;
};


struct MyState {
  MyState(trader_id_t trader_id) :
    trader_id(trader_id), books(), submitted(), open_orders(),
    cash(), positions(), volume_traded(), last_trade_price(100.0),
    log_path("") {}

  MyState() : MyState(0) {}

  void on_trade_update(const Common::TradeUpdate& update) {
    last_trade_price = update.price;

    books[update.ticker].decrease_qty(update.resting_order_id, update.quantity);
    books[update.ticker].print_book(log_path, open_orders);

    if (submitted.count(update.resting_order_id)) {

      if (!submitted.count(update.aggressing_order_id)) {
        volume_traded += update.quantity;
        // not a self-trade
        update_position(update.ticker, update.price,
                        update.buy ? -update.quantity : update.quantity); // opposite, since resting
      }

      open_orders[update.resting_order_id].quantity -= update.quantity;
      if (open_orders[update.resting_order_id].quantity <= 0) {
        open_orders.erase(update.resting_order_id);
      }

    } else if (submitted.count(update.aggressing_order_id)) {
      volume_traded += update.quantity;

      update_position(update.ticker, update.price,
                      update.buy ? update.quantity : -update.quantity);
    }
  }

  void update_position(ticker_t ticker, price_t price, quantity_t delta_quantity) {
    cash -= price * delta_quantity;
    positions[ticker] += delta_quantity;
  }

  void on_order_update(const Common::OrderUpdate& update) {

    const Common::Order order{
      .ticker = update.ticker,
      .price = update.price,
      .quantity = update.quantity,
      .buy = update.buy,
      .ioc = false,
      .order_id = update.order_id,
      .trader_id = trader_id
    };

    books[update.ticker].insert(order);
    books[update.ticker].print_book(log_path, open_orders);

    if (submitted.count(update.order_id)) {
      open_orders[update.order_id] = order;
    }
  }

  void on_cancel_update(const Common::CancelUpdate& update) {
    books[update.ticker].cancel(trader_id, update.order_id);
    books[update.ticker].print_book(log_path, open_orders);

    if (open_orders.count(update.order_id)) {
      open_orders.erase(update.order_id);

    }

    submitted.erase(update.order_id);
  }

  void on_place_order(const Common::Order& order) {
    submitted.insert(order.order_id);
  }


  std::unordered_map<price_t, std::vector<Common::Order>> levels() const {
    std::unordered_map<price_t, std::vector<Common::Order>> levels;
    for (const auto& p : open_orders) {
      const Common::Order& order = p.second;
      levels[order.price].push_back(order);
    }
    return levels;
  }

  price_t get_pnl() const {
    price_t pnl = cash;

    for (int i = 0; i < MAX_NUM_TICKERS; i++) {
      pnl += positions[i] * books[i].get_mid_price(last_trade_price);
    }

    return pnl;
  }

  price_t get_bbo(ticker_t ticker, bool buy) {
    return books[ticker].get_bbo(buy);
  }

  trader_id_t trader_id;
  MyBook books[MAX_NUM_TICKERS];
  std::unordered_set<order_id_t> submitted;
  std::unordered_map<order_id_t, Common::Order> open_orders;
  price_t cash;
  quantity_t positions[MAX_NUM_TICKERS];
  quantity_t volume_traded;
  price_t last_trade_price;
  std::string log_path;

};

class MyBot : public Bot::AbstractBot {

public:

  MyState state;

  using Bot::AbstractBot::AbstractBot;

  static int64_t time_ns() {

    using namespace std::chrono;

    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();

  }
  int64_t last = 0, start_time;

  bool trade_with_me_in_this_packet = false;

  // (maybe) EDIT THIS METHOD
  void init(Bot::Communicator& com) {
    state.trader_id = trader_id;
    // state.log_path = "book.log";
    start_time = time_ns();
  }


  // EDIT THIS METHOD
  void on_trade_update(Common::TradeUpdate& update, Bot::Communicator& com){

    state.on_trade_update(update);

    if (state.submitted.count(update.resting_order_id) ||
        state.submitted.count(update.aggressing_order_id)) {
      trade_with_me_in_this_packet = true;
    }

  }

  // EDIT THIS METHOD
  void on_order_update(Common::OrderUpdate & update, Bot::Communicator& com){
    state.on_order_update(update);

    // NOTE: the strategy here is dumb, and is just to demonstrate the API


    // a way to rate limit yourself
    int64_t now = time_ns();
    if (now - last < 10e6) { // 10ms
      return;
    }

    last = now;

    quantity_t bid_quote = state.books[0].quote_size(true);
    quantity_t ask_quote = state.books[0].quote_size(false);
    quantity_t mkt_volume = 40, bid_volume, ask_volume;
    quantity_t position = state.positions[0];
    price_t bid_price, ask_price, mid_price = state.books[0].get_mid_price(state.last_trade_price), spread = state.books[0].spread();
    price_t best_bid = state.get_bbo(0, true), best_ask = state.get_bbo(0, false);

    if (position > 0) {
      bid_volume = mkt_volume;
      ask_volume = mkt_volume + 0.25 * position;
    } else if (position <= 0) {
      bid_volume = mkt_volume + 0.25 * abs(position);
      ask_volume = mkt_volume;
    }

    double signal = state.books[0].get_signal(30);
    if (signal > 0) {
      ask_price = best_ask + signal*spread;
      bid_price = best_bid + signal*spread;
    } else if (signal < 0) {
      ask_price = best_ask + signal*spread;
      bid_price = best_bid + signal*spread;
    } else {
      return;
    }

    for (const auto& x : state.open_orders) {      
      place_cancel(com, Common::Cancel{
        .ticker = 0,
        .order_id = x.first,
        .trader_id = trader_id
      });
    }
    
    place_order(com, Common::Order{
        .ticker = 0,
        .price = ask_price,
        .quantity = ask_volume,
        .buy = false,
        .ioc = false,
        .order_id = 0, // this order ID will be chosen randomly by com
        .trader_id = trader_id
      });
    place_order(com, Common::Order{
        .ticker = 0,
        .price = bid_price,
        .quantity = bid_volume,
        .buy = true,
        .ioc = false,
        .order_id = 0, // this order ID will be chosen randomly by com
        .trader_id = trader_id
      });
  }

  // EDIT THIS METHOD
  void on_cancel_update(Common::CancelUpdate & update, Bot::Communicator& com){
    state.on_cancel_update(update);
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_order_update(Common::RejectOrderUpdate& update, Bot::Communicator& com) {
    std::cout << update.getMsg() << std::endl;
  }

  // (maybe) EDIT THIS METHOD
  void on_reject_cancel_update(Common::RejectCancelUpdate& update, Bot::Communicator& com) {
    if (update.reason != Common::INVALID_ORDER_ID) {
      std::cout << update.getMsg() << std::endl;
    }
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_start(Bot::Communicator& com) {
    trade_with_me_in_this_packet = false;
  }

  // (maybe) EDIT THIS METHOD
  void on_packet_end(Bot::Communicator& com) {
    if (trade_with_me_in_this_packet) {

      price_t pnl = state.get_pnl();

      std::cout << "got trade with me; pnl = "
                << std::setw(15) << std::left << pnl
                << " ; position = "
                << std::setw(5) << std::left << state.positions[0]
                << " ; pnl/s = "
                << std::setw(15) << std::left << (pnl/((time_ns() - start_time)/1e9))
                << " ; pnl/volume = "
                << std::setw(15) << std::left << (state.volume_traded ? pnl/state.volume_traded : 0.0)
                << std::endl;
    }
  }

  order_id_t place_order(Bot::Communicator& com, const Common::Order& order) {
    Common::Order copy = order;

    copy.order_id = com.place_order(order);

    state.on_place_order(copy);

    return copy.order_id;
  }

  void place_cancel(Bot::Communicator& com, const Common::Cancel& cancel) {
    com.place_cancel(cancel);
  }

};


int main(int argc, const char ** argv) {


  std::string prefix = "comp"; // DO NOT CHANGE THIS

  MyBot* m = new MyBot(Manager::Manager::get_random_trader_id());

  assert(m != NULL);

  Manager::Manager manager;

  std::vector<Bot::AbstractBot*> bots {m};
  manager.run_competitors(prefix, bots);

  return 0;
}
