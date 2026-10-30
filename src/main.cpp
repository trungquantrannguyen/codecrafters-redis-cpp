#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <cctype>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <list>
#include <variant>
#include <iterator>

struct KeyData
{
  std::string value;
  long long expiry;
  long long setTime;
};

struct KeyList
{
  std::list<std::string> list;
};

std::unordered_map<std::string, KeyData>
    dataStorage;

std::unordered_map<std::string, KeyList>
    listStorage;

long long getTimestamp()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::vector<std::string>
parseRESP(const std::string &data)
{
  std::vector<std::string> command;
  size_t pos = 0;

  size_t end = data.find("\r\n", pos);
  int count = std::stoi(data.substr(pos + 1, end - pos - 1));
  pos = end + 2;

  for (int i = 0; i < count; i++)
  {
    end = data.find("\r\n", pos);
    int length = std::stoi(data.substr(pos + 1, end - pos - 1));
    pos = end + 2;

    std::string value = data.substr(pos, length);
    command.push_back(value);

    pos += length + 2;
  }
  return command;
}

void handleClient(int client_fd)
{
  char buffer[1024];

  while (true)
  {
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);

    if (bytes_received <= 0)
    {
      break;
    }

    std::string data(buffer, bytes_received);

    std::vector<std::string> command = parseRESP(data);

    if (command.empty())
      continue;
    std::string cmd = command[0];

    std::transform(
        cmd.begin(),
        cmd.end(),
        cmd.begin(),
        [](unsigned char c)
        {
          return std::toupper(c);
        });

    std::string response;

    if (cmd == "PING")
    {
      response = "+PONG\r\n";
    }
    else if (cmd == "ECHO" && command.size() > 1)
    {
      response = "$" + std::to_string(command[1].size()) + "\r\n" + command[1] + "\r\n";
    }
    else if (cmd == "SET" && command.size() >= 3)
    {
      KeyData data{
          command[2], 0, 0};
      if (command.size() == 5)
      {
        std::string option = command[3];

        std::transform(
            option.begin(),
            option.end(),
            option.begin(),
            [](unsigned char c)
            {
              return std::toupper(c);
            });
        if (option == "PX")
        {
          data.expiry = std::stoll(command[4]);
        }
        if (option == "EX")
        {
          data.expiry = std::stoll(command[4]) * 1000;
        }

        data.setTime = getTimestamp();
      }
      dataStorage[command[1]] = data;
      response = "+OK\r\n";
    }
    else if (cmd == "GET" && command.size() > 1)
    {
      if (dataStorage[command[1]].value.size() > 0)
      {

        if (dataStorage[command[1]].expiry > 0 && getTimestamp() - dataStorage[command[1]].setTime > dataStorage[command[1]].expiry)
        {
          response = "$-1\r\n";
          dataStorage.erase(command[1]);
        }
        else
          response = "$" + std::to_string(dataStorage[command[1]].value.size()) + "\r\n" + dataStorage[command[1]].value + "\r\n";
      }
      else
        response = "$-1\r\n";
    }
    else if (cmd == "RPUSH" && command.size() >= 3)
    {
      for (int i = 2; i < command.size(); i++)
      {
        listStorage[command[1]].list.push_front(command[i]);
      }
      response = ":" + std::to_string(listStorage[command[1]].list.size()) + "\r\n";
    }
    else if (cmd == "LRANGE" && command.size() >= 4)
    {
      if (listStorage[command[1]].list.empty() == 0)
      {
        std::list<std::string> dataList = listStorage[command[1]].list;
        std::string key = command[1];
        int start = std::stoi(command[2]);
        int stop = std::stoi(command[3]);
        if (stop >= dataList.size())
        {
          stop = dataList.size();
          if (start < dataList.size() && start < stop)
          {
            auto first = std::next(dataList.begin(), start);
            auto last = std::next(dataList.begin(), stop);
            std::list<std::string> subLists(first, last);
            response = "*" + std::to_string(subLists.size()) + "\r\n";
            for (std::string subList : subLists)
            {
              response = +"$1\r\n" + subList + "\r\n";
            }

            printf("Response array: %s", response);
          }
          else
          {
            response = "*0\r\n";
          }
        }
      }
      else
      {
        response = "*0\r\n";
      }
    }

    send(client_fd, response.c_str(), response.size(), 0);
  }

  close(client_fd);
}

int main(int argc, char **argv)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment the code below to pass the first stage

  while (true)
  {
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    std::cout << "Client connected\n";
    std::thread(handleClient, client_fd).detach();
  }

  close(server_fd);

  return 0;
}
