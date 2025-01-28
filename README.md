# noevent

This library, named as <u>noevent</u>, works like [libevent](https://github.com/libevent/libevent/tree/patches-1.4) with morden and safe APIs.

## 🛠️ Build

The only requirement is that your compiler supports C++20. After downloading the project, execute the following command in the root directory of this project.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

If you want to build a shared library without any examples, replace the second command mentioned above with the following one.

```bash
cmake .. -DCOMPILE_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON
```

## ✨ Future Works

The initial version of this library was completed within two weeks and still needs improvement. The following are the future to-do items.

- [ ] Optimization of removing any element from the timeout min-heap
- [ ] The implementation of Epoll (Linux) and Select (Windows)
- [ ] More examples
- [ ] Documentation about this library
- [ ] Add more useful features and utils (e.g. read/write buffer)

## 📄 Usage

Since this library is very simple, the most efficient way to learn is through examples. I have provided two simple examples that almost cover all important APIs of this library.

- [examples/echo](https://github.com/yxlau-sleepy/noevent/tree/main/examples/echo): an echo server with timeout.
- [examples/chatroom](https://github.com/yxlau-sleepy/noevent/tree/main/examples/chatroom): a very simple real-time chatroom.

Besides, I think it is quite meaningful to understand the design concept of the library. Also, there are a few points that is prone to error and needs to be clarified.
