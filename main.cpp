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

class TetrisServer {
  public:
    TetrisServer(boost::asio::io_context& io_context, int port)
      : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
      start_accept();
    };

  private:
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

    std::cout << "Chat Server is running on port " << port << "...\n";
    io_context.run();

  } catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  }

  return 0;
}

