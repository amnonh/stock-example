/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright 2015 Cloudius Systems
 */

#include <http/httpd.hh>
#include "gen/stock.json.hh"
#include <http/api_docs.hh>
#include <http/exception.hh>
#include <queue>

namespace bpo = boost::program_options;

using namespace httpd;
using order_status = stock_json::result::result_status;

using trans_id = long;

class stock_order {
public:
    order_status _status;
    unsigned int _amount;
    unsigned int _price;
    trans_id _id;
    stock_order(unsigned int amount = 0, int price = 0, trans_id id = 0) :
        _status(order_status::WAITING),
        _amount(amount), _price(price), _id(id) {
    }
    stock_order(const sstring& amount, const sstring& price) :
            _status(order_status::WAITING),
            _amount(std::stoi(amount)), _price(std::stoi(price)), _id(0) {
        }
    bool operator <=(const stock_order& so) const {
        if (_id == so._id) {
            return true;
        }
        return _price <= so._price;
    }
    bool operator <(const stock_order& so) const {
        if (_id == so._id) {
            return false;
        }
        return _price < so._price;
    }
};

template<bool desc>
class stock_comp {
public:
    bool operator() (const stock_order* lhs, const stock_order* rhs) const {
        if (desc)
            return (*lhs < *rhs);
        return (*rhs < *lhs);
      }
};

class stock {
public:
    stock_json::result  buy(stock_order& so) {
        stock_json::result res;
        res.transactionid = ++_last_id;

        while (!_sell.empty() && _sell.top()->_price <= so._price) {
            stock_order* h = _sell.top();
            _last_price = h->_price;
            if (h->_amount >= so._amount) {
                res.reamining = 0;
                res.status= order_status::DONE;
                if (h->_amount > so._amount) {
                    h->_amount -= so._amount;
                    h->_status = order_status::PARTIAL;
                } else {
                    del_order(h);
                    _sell.pop();
                }

                return res;
            }
            so._amount -= h->_amount;
            so._status = order_status::PARTIAL;
        }
        auto s = new stock_order(so);
        res.status = so._status;
        s->_id = res.transactionid();
        _status[res.transactionid()] = s;
        _buy.push(s);
        return res;
    }
    stock_json::result sell(stock_order& so) {
        stock_json::result res;
        res.transactionid = ++_last_id;

        while (!_buy.empty() && _buy.top()->_price >= so._price) {
            stock_order* h = _buy.top();
            _last_price = h->_price;
            if (h->_amount >= so._amount) {
                res.reamining = 0;
                res.status= order_status::DONE;
                if (h->_amount > so._amount) {
                    std::cout << "get from buy " << h->_amount << std::endl;
                    h->_amount -= so._amount;
                    h->_status = order_status::PARTIAL;
                } else {
                    std::cout << "get from all from buy " << h->_amount << std::endl;
                    del_order(h);
                    _buy.pop();
                }

                return res;
            }
            so._amount -= h->_amount;
            so._status = order_status::PARTIAL;
        }
        auto s = new stock_order(so);
        res.status = so._status;
        s->_id = res.transactionid();
        _status[res.transactionid()] = s;
        _sell.push(s);
        return res;
    }
    stock_json::result status(trans_id id) {
        stock_json::result res;
        if (id >= _last_id) {
            throw not_found_exception("The requested transaction id does not found");
        }
        res.transactionid = id;
        if (_status.find(id) == _status.end()) {
            res.status = order_status::DONE;
            return res;
        }
        auto h = _status[id];
        res.status = h->_status;
        res.reamining = h->_amount;
        return res;
    }
    stock() : _last_price(-1), _last_id(0) {}
    float price() const {
        return _last_price;
    }
private:
    void del_order(stock_order* h) {
        _status.erase(h->_id);
        delete h;
    }
    //auto comp = [](const stock_order& a, const stock_order& b) { return b < a;};
    std::priority_queue<stock_order*, std::vector<stock_order*>, stock_comp<true>> _buy;
    std::priority_queue<stock_order*, std::vector<stock_order*>, stock_comp<false> > _sell;
    std::unordered_map<trans_id, stock_order*> _status;
    float _last_price;
    trans_id _last_id;
};


class stock_exchange {
    std::unordered_map<sstring, stock> _stocks;
public:
    static unsigned short get_id (const sstring& stock) {
        unsigned short res = 0;
        for (auto i : stock) {
            res ^= i;
        }
        return res % smp::count;
    }
    future<json::json_return_type> buy_sell(std::unique_ptr<request> req) {
        const sstring& stockid = req->param.at("stockid");
        std::cout << "buy_sell " <<stockid << std::endl;
        if (_stocks.find(stockid) == _stocks.end()) {
            _stocks[stockid] = stock();
        }
        stock_order so(req->get_query_param("amount"), req->get_query_param("price"));
        if (req->get_query_param("op") == "Buy") {
            return make_ready_future<json::json_return_type>(_stocks[stockid].buy(so));
        }
        return make_ready_future<json::json_return_type>(_stocks[stockid].sell(so));
    }

    future<json::json_return_type> get_price(std::unique_ptr<request> req) const {
        if (_stocks.find(req->param.at("stockid")) == _stocks.end()) {
            throw not_found_exception("Stock not found");
        }
        return make_ready_future<json::json_return_type>(_stocks.at(req->param.at("stockid")).price());
    }
    future<json::json_return_type> get_status(std::unique_ptr<request> req) {
        return make_ready_future<json::json_return_type>("DONE");
    }


};


void set_routes(routes& r, distributed<stock_exchange>* se) {
    stock_json::buy_sell_cmnd.set(r, [se](std::unique_ptr<request> req) {
        auto cpu = stock_exchange::get_id(req->param.at("stockid"));
        std::cout << "buy-sell cpu " << cpu << " local " << engine().cpu_id() << std::endl;
        return se->invoke_on(cpu, [req = std::move(req)](stock_exchange& s) mutable {
            return s.buy_sell(std::move(req));
        });
    });
    stock_json::get_price.set(r, [se](std::unique_ptr<request> req) {
        auto cpu = stock_exchange::get_id(req->param.at("stockid"));
        std::cout << "cpu " << cpu << " local " << engine().cpu_id() << std::endl;
        return se->invoke_on(cpu, [req = std::move(req)](stock_exchange& s) mutable {
            return s.get_price(std::move(req));
        });
    });
}

int main(int ac, char** av) {
    app_template app;
    auto sechange = new distributed<stock_exchange>;
    app.add_options()("port", bpo::value<uint16_t>()->default_value(10000),
            "HTTP Server port");
    return app.run(ac, av,
            [&] {
                auto&& config = app.configuration();
                uint16_t port = config["port"].as<uint16_t>();
                auto server = make_shared<http_server_control>();
                auto rb = make_shared<api_registry_builder>("./");
                sechange->start().then([server] {
                    return server->start();
                }).then([server, sechange, rb] {
                    return server->set_routes([sechange, rb](routes& r) {
                        rb->set_api_doc(r);
                        rb->register_function(r, "stock", "Stock exchange demo application");
                        set_routes(r, sechange);

                    });
                }).then([server, port] {
                    return server->listen(port);
                }).then([port] {
                    std::cout << "Seastar HTTP server listening on port " << port << " ...\n";
                });

            });
}
