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
#include <cstdlib>
#include <math.h> 


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

  price_t get_mid_price(price_t default_to) const {
    price_t best_bid = get_bbo(true);
    price_t best_offer = get_bbo(false);

    if (best_bid == 0.0 || best_offer == 0.0) {
      return default_to;
    }

    return 0.5 * (best_bid + best_offer);

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

    // Calculate bid volume for all levels
    /*
    quantity_t bid_volume = 0;
    for (auto& x : sides[bid]) {
      if (x.price > best_bid) {
        break;
      }
      bid_volume += x.quantity;
    }
    */

    // Calculate  ask volume for all levels
    /*
    quantity_t ask_volume = 0;
    for (auto& x : sides[ask]) {
      if (x.price < best_offer) {
        break;
      }
      ask_volume += x.quantity;
    }
    */

    double weight;
    // Calculate bid volume for up to n levels
    quantity_t bid_volume = 0;
    std::set<LimitOrder>::iterator bid_level=sides[1].begin();
    for (int i = 0; i<num_levels && bid_level!=sides[1].end(); i++) {
      //if (i == 0 && bid_level -> quantity >= 1000) {
      //  bid_level++;
      //  continue; 
      //}
      weight = 1 - abs(best_bid - bid_level->price)/best_bid;
      bid_volume += weight * (bid_level->quantity);
      bid_level++;
    }

    // Calculate ask volume for up to n levels
    quantity_t ask_volume = 0;
    std::set<LimitOrder>::iterator ask_level=sides[0].begin();
    for (int i = 0; i<num_levels && ask_level!=sides[0].end(); i++) {
      //if (i == 0 && ask_level -> quantity >= 1000) {
      //  ask_level++;
      //  continue; 
      //}
      weight = 1 - abs(ask_level->price - best_offer)/best_offer;
      ask_volume += weight * (ask_level->quantity);
      ask_level++;
    }
      

    // Calculate signal
    signal = ((double)bid_volume - (double)ask_volume)/((double)bid_volume + (double)ask_volume);

    return signal;
  }

  int get_quote_vol(bool buy) {
    quantity_t vol = 0;
    for (std::set<LimitOrder>::iterator level=sides[buy].begin(); level!=sides[buy].end(); ++level) {
      vol += level->quantity;
    }
    return vol;
  }

  price_t get_second_price(bool buy) {
    std::set<LimitOrder>::iterator level=sides[buy].begin();
    price_t price = level->price;
    return price;
  }

  int num_levels(bool buy) {
    int count = 0;
    for (std::set<LimitOrder>::iterator level = sides[buy].begin(); level!=sides[buy].end(); level++)
      count+=1;

    return count;
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

    fout << " -- Next Time Step -- \n";
    fout << "Signal: " << get_signal(8) << "\n";
    fout << "Spread: " << spread() << "\n";
    fout << "Sell Quote Size: " << quote_size(false) << "\n";
    fout << "Sell Vol: " << get_quote_vol(false) << "\n";
    fout << "Buy Quote Size: " << quote_size(true) << "\n";
    fout << "Buy Vol: " << get_quote_vol(true) << "\n";
    fout << "Midpoint: " << get_mid_price(0) << "\n";


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
    log_path(""), long_term_orders() {}

  MyState() : MyState(0) {}

  void on_trade_update(const Common::TradeUpdate& update) {
    last_trade_price = update.price;

    books[update.ticker].decrease_qty(update.resting_order_id, update.quantity);
    // books[update.ticker].print_book(log_path, open_orders);

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

      if (long_term_orders.find(update.resting_order_id) != long_term_orders.end()) {
        long_term_orders.erase(update.resting_order_id);
      }

    } else if (submitted.count(update.aggressing_order_id)) {
      volume_traded += update.quantity;

      if (long_term_orders.find(update.aggressing_order_id) != long_term_orders.end()) {
        long_term_orders.erase(update.aggressing_order_id);
      }

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
    // books[update.ticker].print_book(log_path, open_orders);

    if (submitted.count(update.order_id)) {
      open_orders[update.order_id] = order;
    }
  }

  void on_cancel_update(const Common::CancelUpdate& update) {
    books[update.ticker].cancel(trader_id, update.order_id);
    // books[update.ticker].print_book(log_path, open_orders);

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

  double get_signal(ticker_t ticker, int num_levels) {
    return books[ticker].get_signal(num_levels);
  }

  price_t get_mid_price(ticker_t ticker) {
    return books[ticker].get_mid_price(0.0);
  }

  price_t get_spread(ticker_t ticker) {
    return books[ticker].spread();
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
  std::set<order_id_t> long_term_orders;

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
  int64_t cycle = 0;
  double sum_diff_in_spread = 0;
  double num_updates = 0;
  double previous_spread = 0;
  double previous_signal = 0;
  double cumulative_signal_over_second = 0;
  int num_signals = 0;
  int previous_directional_bet_is_bid = -1; // 1 is bet, 0 is ask, -1 is undefined

  bool trade_with_me_in_this_packet = false;

  // User deifined parameters
  int num_levels_for_signal;
  quantity_t mkt_volume;
  double meaningful_signal_diff;
  
  quantity_t bid_volume, ask_volume, position, bid_quote, ask_quote;
  double best_bid, best_offer, signal_difference, mid_price, spread, signal, t_minus_one_signal=2, t_minus_two_signal=2, avg_signal=0, previous_avg_signal, second_price;


  int64_t now;



  // (maybe) EDIT THIS METHOD
  void init(Bot::Communicator& com) {
    state.trader_id = trader_id;
    state.log_path = "book.log";

    // Query User for input
    std::cout << "Please input the number of levels you want to use for signal (Suggested: 8 or 25): ";
    std::cin >> num_levels_for_signal;
    std::cout << "\nPlease input your market volume (Suggested: 20 or 20):  ";
    std::cin >> mkt_volume;
    std::cout << "\nPlease input signal difference needed to update market (Suggested: 0.1 or 0.2): ";
    std::cin >> meaningful_signal_diff;
    std::cout << "\n";

    start_time = time_ns();
    cycle = time_ns();
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

    best_bid = state.get_bbo(0, true);
    best_offer = state.get_bbo(0, false);
    mid_price = 0.5 * (best_bid + best_offer);
    spread = best_offer - best_bid;

    signal = state.get_signal(0, num_levels_for_signal);
    position = state.positions[0];
    num_updates += 1;

    now = time_ns();
    last = now;

    if (t_minus_one_signal == 2) {
      t_minus_one_signal = signal;
      return;
    } else if (t_minus_two_signal == 2) {
      t_minus_two_signal = t_minus_one_signal;
      t_minus_one_signal = signal;
      return;
    } else {
      previous_avg_signal = avg_signal;
      avg_signal = (t_minus_two_signal + t_minus_one_signal + signal)/3;
      t_minus_two_signal = t_minus_two_signal;
      t_minus_one_signal = signal;
    }

    // state.books[0].print_book(state.log_path, state.open_orders);

    if (now - cycle > 1e8) {
      cycle = now;

      /*
      if (position > 20) {
        place_order(com, Common::Order{
          .ticker = 0,
          .price = best_bid-0.05,
          .quantity = position,
          .buy = false,
          .ioc = true,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
        return;
      } else if (position < -20) {
        place_order(com, Common::Order{
          .ticker = 0,
          .price = best_offer+0.05,
          .quantity = position,
          .buy = true,
          .ioc = true,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
        return;
      }
      */
    }
    bid_quote = state.books[0].quote_size(true);
    ask_quote = state.books[0].quote_size(false);

    /*
    std::cout << "Bid Quote: " << bid_quote << "\n";
    std::cout << "Ask Quote: " << ask_quote << "\n";
    std::cout << "Mid Price: " << mid_price << "\n";
    std::cout << "Get Second Ask Price: " << state.books[0].get_second_price(false) << "\n\n";
    */

    if (position > 80) {
      bid_volume = mkt_volume;
      ask_volume = mkt_volume + 0.5 * position;
    } else if (position < -80) {
      bid_volume = mkt_volume + 0.5 * abs(position);
      ask_volume = mkt_volume;
    } else {
      bid_volume = mkt_volume;
      ask_volume = mkt_volume;
    }
    
    if (ask_quote - bid_quote > 5000) {
      state.long_term_orders.insert(place_order(com, Common::Order{
          .ticker = 0,
          .price = state.books[0].get_second_price(false)-0.01,
          .quantity = ask_volume,
          .buy = false,
          .ioc = false,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        }));
    } else if (bid_quote - ask_quote > 5000) {
      state.long_term_orders.insert(place_order(com, Common::Order{
          .ticker = 0,
          .price = state.books[0].get_second_price(true)+0.01,
          .quantity = bid_volume,
          .buy = true,
          .ioc = false,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        }));
    }

    /* --------------- MAKER - MAKER STRATEGY START------------------- */
    signal_difference = abs(avg_signal - previous_avg_signal);
    if (signal_difference == 0) {
      return;
    } else if (avg_signal > 0) {
      // Move midprice up proportional to signal
      if (abs(avg_signal) > 0) {
        // Cancel all open orders

        
        for (const auto& x : state.open_orders) {
            if (state.long_term_orders.find(x.first) == state.long_term_orders.end()) {
              place_cancel(com, Common::Cancel{
                .ticker = 0,
                .order_id = x.first,
                .trader_id = trader_id
              });
            }
        }

        // Make new market
        place_order(com, Common::Order{
          .ticker = 0,
          .price = mid_price,
          .quantity = bid_volume,
          .buy = true,
          .ioc = true,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
        place_order(com, Common::Order{
          .ticker = 0,
          .price = state.books[0].get_second_price(false)-0.01,
          .quantity = ask_volume,
          .buy = false,
          .ioc = false,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
      }
    } else if (avg_signal < 0) {
      // Move midprice down proportional to signal
      if (abs(avg_signal) > 0) {
        // Cancel all open orders

        
        for (const auto& x : state.open_orders) {
          if (state.long_term_orders.find(x.first) == state.long_term_orders.end()) {
            place_cancel(com, Common::Cancel{
              .ticker = 0,
              .order_id = x.first,
              .trader_id = trader_id
            });
          }
        }

        // Make new market
        place_order(com, Common::Order{
          .ticker = 0,
          .price = state.books[0].get_second_price(true)+0.01,
          .quantity = bid_volume,
          .buy = true,
          .ioc = false,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
        place_order(com, Common::Order{
          .ticker = 0,
          .price = mid_price,
          .quantity = ask_volume,
          .buy = false,
          .ioc = true,
          .order_id = 0, // this order ID will be chosen randomly by com
          .trader_id = trader_id
        });
      }
      
    }
    // previous_signal = signal;
    // previous_avg_signal = avg_signal;
   /* --------------- MAKER - MAKER STRATEGY END------------------- */
  }

  // EDIT THIS METHOD
  void on_cancel_update(Common::CancelUpdate & update, Bot::Communicator& com){
    state.on_cancel_update(update);
  }

  // (maybe) EDIT THIS METHOD std::cout << update.getMsg() << std::endl;
  void on_reject_order_update(Common::RejectOrderUpdate& update, Bot::Communicator& com) {
  }

  // (maybe) EDIT THIS METHOD std::cout << update.getMsg() << std::endl;
  void on_reject_cancel_update(Common::RejectCancelUpdate& update, Bot::Communicator& com) {
    if (update.reason != Common::INVALID_ORDER_ID) {
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

      /*
      std::cout << "got trade with me; pnl = "
                << std::setw(15) << std::left << pnl
                << " ; position = "
                << std::setw(5) << std::left << state.positions[0]
                << " ; pnl/s = "
                << std::setw(15) << std::left << (pnl/((time_ns() - start_time)/1e9))
                << " ; pnl/volume = "
                << std::setw(15) << std::left << (state.volume_traded ? pnl/state.volume_traded : 0.0)
                << std::endl;
    */
    }
  }

  order_id_t place_order(Bot::Communicator& com, const Common::Order& order) {
    Common::Order copy = order;

    copy.order_id = com.place_order(order);

    state.on_place_order(copy);

    return copy.order_id;
  }

  void place_cancel(Bot::Communicator& com, const Common::Cancel& cancel) {
    // std::cout << "Canceling order";
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
