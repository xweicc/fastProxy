# 支持自动选路的Socks5代理
### 使用说明
- 编译：执行 `make`， 生成可执行程序`proxyClient`和`proxyServer`
- 服务端执行：`./proxyServer <serverPort>`
- 客户端执行：`./proxyClient <clientPort> <serverIp> <serverPort> 1`
### 功能说明
- 支持HTTP，TCP，SOCKS5代理
- 当有连接请求时，会同时从客户端和服务端解析DNS并建立连接，自动选择最快的连接
- 数据加密，HTTP伪装
- DNS解析和TCP Connect异步处理，并发连接更快
