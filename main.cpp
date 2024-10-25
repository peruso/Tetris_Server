#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <string>
#include <memory>
#include <signal.h>

using boost::asio::ip::tcp;

struct Position {
  int pos_row = 0;
  int pos_column = 0; 
};
struct BlockData {
  std::array<Position, 4> positions;
  int indexColor;
};
struct MultiplayerData {
std::string name;
std::string score;
};

class TetrisServer {
  public:
    TetrisServer(boost::asio::io_context& io_context, int port)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
      start_accept();
    };

  private:
    std::vector<int> listPlayerWithConsensus;
    void start_accept() {
      auto socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
      acceptor_.async_accept(*socket, [this, socket](boost::system::error_code ec) {
        if (!ec) {
          std::lock_guard<std::mutex> lock(clients_mutex_);
          clients_.push_back(socket);
          std::thread(&TetrisServer::handle_client, this, socket).detach();
          std::cout <<"Connection established" <<std::endl;
        }
        start_accept();  // 再度受け付けを開始
      });
    }

//Response to signal fm client
    void handle_client(std::shared_ptr<tcp::socket> socket) {
      try {
        // 受信バッファサイズの確認
        boost::asio::socket_base::receive_buffer_size option;
        socket->get_option(option);
        
        // int grids[20][10];
        while (true) {
          // memset(grids, 0, sizeof(grids));

          boost::system::error_code ec;
          // size_t bytes_read = boost::asio::read(*socket, boost::asio::buffer(grids, sizeof(grids)), ec);
          identifyReceivedData(socket);


          if (ec) {
              std::cerr << "Error reading from client: " << ec.message() << std::endl;
              break;  // ループを終了して接続をクリーンアップ
          }

                
        }
        // std::cout << "Current receive buffer size: " << option.value() << " bytes" << std::endl;
      } catch (std::exception& e) {
        std::cerr << "Client Connection Error: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), socket), clients_.end());
      }
    }

    void processGridData (int (&grids)[20][10], std::shared_ptr<tcp::socket> sender_socket) {
      std::cout << "Received grid data from client:\n";
                for (int row = 0; row < 20; ++row) {
                    for (int col = 0; col < 10; ++col) {
                        std::cout << grids[row][col] << " ";
                    }
                    std::cout << std::endl;
                }

                // 他のクライアントにもデータをブロードキャスト
      broadcast_grid_data(grids, sender_socket);
    }

    void processBlockData (BlockData& blockData, std::shared_ptr<tcp::socket> sender_socket) {
        std::cout << "Received block data:\n";
        for (const auto& pos : blockData.positions) {
          std::cout << "Position: (" << pos.pos_row << ", " << pos.pos_column << ")\n";
        }
        std::cout << "Block color index: " << blockData.indexColor << std::endl;
        broadcast_block_data(blockData, sender_socket);
    }

    void processPlayerData (MultiplayerData& playerData, std::shared_ptr<tcp::socket> sender_socket) {
      std::cout << "Received player data:\n";
        
        std::cout << "Player Name: " << playerData.name << std::endl;
        std::cout << "Player Score: " << playerData.score << std::endl;
        broadcast_player_data(playerData, sender_socket);
    }

    void identifyReceivedData(std::shared_ptr<tcp::socket> socket) {
      int identifier;
      boost::asio::read(*socket, boost::asio::buffer(&identifier, sizeof(int)));
      if (identifier == 0) {
        // グリッドデータを受け取る
        int grids[20][10];
        boost::asio::read(*socket, boost::asio::buffer(grids, sizeof(int) * 20 * 10));
        // グリッドデータを処理
        processGridData(grids, socket);

    } else if (identifier == 1) {
        // ブロックデータを受け取る
        BlockData blockData;
        boost::asio::read(*socket, boost::asio::buffer(blockData.positions.data(), sizeof(Position) * 4));
        boost::asio::read(*socket, boost::asio::buffer(&blockData.indexColor, sizeof(int)));
        // ブロックデータを処理
        processBlockData(blockData, socket); 
    } else if (identifier == 2) {
      int clientCount = getClientCount();
      boost::asio::write(*socket, boost::asio::buffer(&clientCount, sizeof(int)));
      // for (auto& client : clients_) {
      //     if (client->is_open()) {
      //         try {
      //             boost::asio::write(*client, boost::asio::buffer(&clientCount, sizeof(int)));
      //         } catch (const std::exception& e) {
      //             std::cerr << "Error sending clientCount: " << e.what() << std::endl;
      //         }
      //     }
      // }
      std::cout << "Sent client count: " << clientCount << std::endl;
    } else if (identifier == 3) {
      bool isAbleToStartGame = takeConsensusToStartGame();
      boost::asio::write(*socket, boost::asio::buffer(&isAbleToStartGame, sizeof(bool)));
    } else if (identifier == 4) {
    MultiplayerData playerData;
    int nameLength;
    int scoreLength;

    // 名前の長さを読み込む
    boost::asio::read(*socket, boost::asio::buffer(&nameLength, sizeof(int)));

    // 名前自体を読み込むバッファを確保し、その後 playerData.name にセット
    std::vector<char> nameBuffer(nameLength);
    boost::asio::read(*socket, boost::asio::buffer(nameBuffer.data(), nameLength));
    playerData.name = std::string(nameBuffer.begin(), nameBuffer.end());

    // スコアの長さを読み込む
    boost::asio::read(*socket, boost::asio::buffer(&scoreLength, sizeof(int)));

    // スコア自体を読み込むバッファを確保し、その後 playerData.score にセット
    std::vector<char> scoreBuffer(scoreLength);
    boost::asio::read(*socket, boost::asio::buffer(scoreBuffer.data(), scoreLength));
    playerData.score = std::string(scoreBuffer.begin(), scoreBuffer.end());

    // データを処理
    processPlayerData(playerData, socket);
}

  }
    

     // 受信したグリッドデータを他のクライアントに送信する
    // void broadcast_grid_data(int (&grids)[20][10]) {
    //     std::lock_guard<std::mutex> lock(clients_mutex_);
    //     std::cout << "passed broadcast area" <<std::endl;
    //     std::cout << "clients_ " << clients_.empty()<<std::endl;
    //     for (auto& client : clients_) {
    //         if (client->is_open()) {
    //             boost::asio::write(*client, boost::asio::buffer(grids, sizeof(int) * 20 * 10));
    //             std::cout << "client is open" <<std::endl;
    //         }
    //         for (int row = 0; row < 20; ++row) {
    //                 for (int col = 0; col < 10; ++col) {
    //                     std::cout << grids[row][col] << " ";
    //                 }
    //                 std::cout << std::endl;
    //             }
    //     }
    // }

    int getClientCount() {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      return clients_.size();
    }

    bool takeConsensusToStartGame() {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      listPlayerWithConsensus.push_back(1);
      if (listPlayerWithConsensus.size() >=2) {
        return true;
      }
      else {
        return false;
      }

    }

    void broadcast_block_data(BlockData& blockData, std::shared_ptr<tcp::socket> sender_socket) {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      std::cout << "Broadcasting block data to clients...\n";

      int identifier = 1; // for block data
      std::array<char, sizeof(Position) * 4 + sizeof(int) + sizeof(int)> buffer;
      std::memcpy(buffer.data(), &identifier, sizeof(int));  // 先頭に識別子を追加
      std::memcpy(buffer.data() + sizeof(int), blockData.positions.data(), sizeof(Position) * 4);  // ブロックの位置データをコピー
      std::memcpy(buffer.data() + sizeof(int) + sizeof(Position) * 4, &blockData.indexColor, sizeof(int));  // indexColorをコピー
      for (auto& client : clients_) {
          if (client != sender_socket && client->is_open()) {
              try {
                  boost::asio::write(*client, boost::asio::buffer(buffer));
                  std::cout << "Block data sent to a client.\n";
              } catch (const std::exception& e) {
                  std::cerr << "Error sending block data to client: " << e.what() << std::endl;
              }
          }
      }
    }

    void broadcast_player_data(MultiplayerData& playerData, std::shared_ptr<tcp::socket> sender_socket) {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      std::cout << "Broadcasting player data to clients...\n";

      int identifier = 4; // for player data
      int nameLength = playerData.name.size();

      int scoreLength = playerData.score.size();

    // Calculate buffer size to accommodate both strings and their lengths
    std::size_t bufferSize = sizeof(int)           // identifier
                           + sizeof(int)           // name length
                           + nameLength            // name itself
                           + sizeof(int)           // score length
                           + scoreLength;          // score itself

      std::vector<char> buffer(bufferSize); // 動的なバッファ作成
      char* ptr = buffer.data();

      // 識別子をバッファに追加
      std::memcpy(ptr, &identifier, sizeof(int));
      ptr += sizeof(int);

      // 名前の長さをバッファに追加
      std::memcpy(ptr, &nameLength, sizeof(int));
      ptr += sizeof(int);

      // 名前自体をバッファに追加
      std::memcpy(ptr, playerData.name.c_str(), nameLength);
      ptr += nameLength;

      // Copy the score length into the buffer
      std::memcpy(ptr, &scoreLength, sizeof(int));
      ptr += sizeof(int);

      // Copy the score itself into the buffer
      std::memcpy(ptr, playerData.score.c_str(), scoreLength);

      for (auto& client : clients_) {
          if (client != sender_socket && client->is_open()) {
              try {
                  boost::asio::write(*client, boost::asio::buffer(buffer));
                  std::cout << "Player data sent to a client.\n";
              } catch (const std::exception& e) {
                  std::cerr << "Error sending block data to client: " << e.what() << std::endl;
              }
          }
      }
    }

    void broadcast_grid_data(int (&grids)[20][10], std::shared_ptr<tcp::socket> sender_socket) {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      std::cout << "Broadcasting grid data to clients...\n";

      // Identifier for grid data
      int identifier = 0;
      
      // Create a buffer to hold the identifier and grid data
      std::array<char, sizeof(int) * (20 * 10 + 1)> buffer;
      
      // Copy the identifier to the buffer
      std::memcpy(buffer.data(), &identifier, sizeof(int));
      
      // Copy the grid data after the identifier
      std::memcpy(buffer.data() + sizeof(int), grids, sizeof(int) * 20 * 10);
      
      // Iterate over all clients and send the data
      for (auto& client : clients_) {
          if (client != sender_socket && client->is_open()) {
              try {
                  boost::asio::write(*client, boost::asio::buffer(buffer));
                  std::cout << "Grid data sent to a client.\n";
              } catch (const std::exception& e) {
                  std::cerr << "Error sending grid data to client: " << e.what() << std::endl;
              }
          }
      }
    }


    tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<tcp::socket>> clients_;
    std::mutex clients_mutex_;
};

boost::asio::io_context* global_io_context = nullptr;


void signal_handler(int) {
  if (global_io_context) {
    global_io_context->stop();
  }
}

int main() {
  try {
    boost::asio::io_context io_context;
    global_io_context = &io_context;  // グローバル変数に保存

    // シグナルハンドラの設定 (Ctrl+C や SIGTERM をキャッチして停止処理を行う)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int port = 12345;
    TetrisServer server(io_context, port);

    std::cout << "Server is running on port " << port << "...\n";
    io_context.run();

  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }

  return 0;
}

