#pragma once

#include "Logic.h"

class StdioInterface {
public:
  explicit StdioInterface(Logic* logic);

  void run();

private:
  void handle_request(Response& response, const Request& request);
  void handle_error(Response& response,
      const json::Document& request, const std::exception& ex);
  bool read(std::vector<char>& buffer);
  void write(std::string_view message);

  Logic& m_logic;
};
