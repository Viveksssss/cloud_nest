#include <HttpContext.h>
#include <HttpServer.h>
#include <iostream>
#include <Logger.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

int main(int, char **) {
    try {
        // 数据库连接信息
        pqxx::connection conn("host=127.0.0.1 port=5432 dbname=vivek user=vivek password=38324");

        if (conn.is_open()) {
            std::cout << "成功连接到数据库: " << conn.dbname() << std::endl;
            // 创建事务对象并执行查询
            pqxx::work txn(conn);

            
            pqxx::result res = txn.exec("SELECT version();");
            txn.commit();

            // 输出查询结果
            std::cout << "PostgreSQL版本: " << res[0][0].c_str() << std::endl;
        } else {
            std::cerr << "无法连接到数据库" << std::endl;
        }
    } catch (std::exception const &e) {
        std::cerr << "发生异常: " << e.what() << std::endl;
    }
}
