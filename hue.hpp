#ifndef HUE_HPP
#define HUE_HPP

#include <httplib.h>
#include <json/json.h>
#include <vector>
#include <string>
#include <map>
#include <stdexcept>

namespace hue {
  // Forward Declarations
  class event;
  class light;
  class state;
  class user;
  class bridge;

  using light_states = std::map<light, state>;
  
  class event {
    friend class state;

    friend std::string to_json(const event& e) {
      Json::StyledWriter writer;
      return writer.write(e._self->_root);
    }
  private:
    event(Json::Value root) : _self(std::make_shared<const model>(std::move(root))) {}

    struct model {
      Json::Value _root;
      model(Json::Value root) : _root(std::move(root)) {}
    };

    std::shared_ptr<const model> _self;
  };

  class light {
    friend class bridge;

    friend bool operator<(const light& lhs, const light& rhs) {
      return lhs._self < rhs._self;
    }

    friend std::string get_id(const light& l) {
      return l._self->_id;
    }

  private:
    light(std::string id, std::string name) : _self(std::make_shared<const model>(std::move(id), std::move(name))) {}
    
    struct model {
      std::string _id;
      std::string _name;

      model(std::string id, std::string name) : _id(std::move(id)), _name(std::move(name)) {}
    };

    std::shared_ptr<const model> _self;
  };

  class state {
    friend class bridge;

    friend std::string to_json(const light_states& states) {
      Json::Value root;
      for (auto& light_pair : states) {
        auto& state = light_pair.second;
        Json::Value light;
        light["id"] = state._self->_id;
        light["name"] = state._self->_name;
        light["on"] = state._self->_on;
        light["brightness"] = state._self->_bri;
        root.append(light);
      }
      Json::StyledWriter writer;
      return writer.write(root);
    }
    friend std::vector<event> get_events(const state& current, const state& next) {
      std::vector<event> events;
      if (current._self->_id != next._self->_id) throw std::logic_error("states are not for the same light");
      if (current._self == next._self) return events;

      Json::Value base_event;
      base_event["id"] = next._self->_id;

      if (current._self->_name != next._self->_name) {
        auto event_json = base_event;
        event_json["name"] = next._self->_name;
        events.push_back(event(std::move(event_json)));
      }
      if (current._self->_on != next._self->_on) {
        auto event_json = base_event;
        event_json["on"] = next._self->_on;
        events.push_back(event(std::move(event_json)));
      }
      if (current._self->_bri != next._self->_bri) {
        auto event_json = std::move(base_event);
        event_json["brightness"] = next._self->_bri;
        events.push_back(event(std::move(event_json)));
      }
      return events;
    }
  private:
    state(std::string id, std::string name, bool on, int bri) : _self(std::make_shared<const model>(std::move(id), std::move(name), on, bri)) {}

    struct model {
      std::string _id;
      std::string _name;
      bool _on;
      int _bri;
      model(std::string id, std::string name, bool on, int bri) : _id(std::move(id)), _name(std::move(name)), _on(on), _bri(bri) {}
    };

    std::shared_ptr<const model> _self;
  };

  class user {
    friend class bridge;

    friend std::string get_name(const user& u) {
      return u._self->_username;
    }
  private:
    user(std::string username) : _self(std::make_shared<const model>(std::move(username))) {}

    struct model {
      std::string _username;
      model(std::string username) : _username(std::move(username)) {}
    };

    std::shared_ptr<const model> _self;
  };

  class bridge {
    friend user create_login(const bridge& b, std::string device_name) { 
      auto client = get_client(b);
      
      Json::Value request_root;
      request_root["devicetype"] = std::string("josh_test_app#") + device_name;
      
      Json::FastWriter writer;
      auto res = client.post("/api", writer.write(request_root), "application/json");
      if (!res || res->status != 200) throw std::runtime_error("failed to create user");

      Json::Value root;
      Json::Reader reader;

      if (!reader.parse(res->body.c_str(), root)) throw std::runtime_error("failed to parse json from create user");

      const Json::Value& result = root[static_cast<Json::Value::ArrayIndex>(0)];
      
      const Json::Value& error = result["error"];
      if (!!error) throw std::runtime_error("link-button not pressed");

      const Json::Value& success = result["success"];
      if (!success) throw std::runtime_error("no error, but no success either");

      return user(success["username"].asString());
    }

    friend std::vector<light> get_lights(const bridge& b, const user& u) {
      auto client = get_client(b);
      
      auto res = client.get((std::string("/api/") + get_name(u) + "/lights").c_str());
      if (!res || res->status != 200) throw std::runtime_error("failed to get lights");

      std::vector<light> lights;

      Json::Value root;
      Json::Reader reader;

      if (!reader.parse(res->body.c_str(), root)) throw std::runtime_error("failed to parse json from get lights");

      // TODO(aalbers): Add check if auth failed due to user being deleted. Simulator does not support this test.

      for (auto itr = root.begin(); itr != root.end(); ++itr) {
        std::string id = itr.key().asString();
        std::string name = (*itr)["name"].asString();
        
        lights.push_back(light(std::move(id), std::move(name)));
      }

      return lights;
    }

    friend state get_light_state(const bridge& b, const user& u, const light& l) { 
      auto client = get_client(b);
      
      auto res = client.get((std::string("/api/") + get_name(u) + "/lights/" + get_id(l)).c_str());
      if (!res || res->status != 200) throw std::runtime_error("failed to get light state");
      
      Json::Value root;
      Json::Reader reader;

      if (!reader.parse(res->body.c_str(), root)) throw std::runtime_error("failed to parse json from light state");
      
      // TODO(aalbers): Add check if auth failed due to user being deleted. Simulator does not support this test.
      
      const Json::Value& json_state = root["state"];

      std::string id = get_id(l);
      std::string name = root["name"].asString();
      bool on = json_state["on"].asBool();
      int bri = (json_state["bri"].asInt() / 254.0f) * 100; // Map range 1-254 to 0-100 (Slight lose of precision)
      // Note: The simulator allows for bri values greater than 254 while the docs specifically say it is capped
      // That case would have to be handled if greater than 254 values where possible in the wild
      // Otherwise percentages greater than 100 would be possible

      return state(std::move(id), std::move(name), on, bri);
    }

    friend light_states get_light_states(const bridge& b, const user& u, const std::vector<light>& lights) {
      light_states states;
      for (auto& light : lights) {
        auto state = get_light_state(b, u, light);
        states.emplace(light, std::move(state));
      }
      return states;
    }
  public:
    bridge(std::string hostname, uint16_t port) : _self(std::make_shared<const model>(std::move(hostname), port)) {}
  
  private:
    static httplib::Client get_client(const bridge& b) {
      return httplib::Client(b._self->_hostname.c_str(), b._self->_port);
    }

    struct model {
      std::string _hostname;
      uint16_t _port;
      model(std::string hostname, uint16_t port) : _hostname(std::move(hostname)), _port(port) {}
    };
  
    std::shared_ptr<const model> _self;
  };
}

#endif // HUE_HPP
