# 🌟 Interpreter Playground 🌟

This project is an interactive, browser-based implementation of an interpreter implemented in C.  

🧑‍💻 **Try it out now:** [Live Demo](https://AntonVucinic.github.io/lang)  

---

## ✨ Features
- **Write and Execute**: Run code directly in your browser.
- **Dynamic Feedback**: View program output or errors instantly.
- **Examples at Your Fingertips**: Select from built-in code snippets to get started quickly.

---

## 🛠️ Building the Project

This project can be built in two ways:

### 1️⃣ **Local Execution**
If you'd like to run the interpreter locally on your machine:
1. Make sure you have a C compiler and `make`
1. Simply run
   ```bash
   make
   ```
This will allow you to execute programs directly from your terminal.

---

### 2️⃣ **Browser Execution (WebAssembly)**
If you'd like to run the interpreter in the browser, follow these steps:
1. Install [Emscripten](https://emscripten.org/) and make
1. Build the WebAssembly module using the `lox.js` target in the `Makefile`:
   ```bash
   emmake make lox.js
   ```
1. Start a local fileserver e.g.:
   ```bash
   python -m http.server
   ```
   This step is needed because [not all browsers support `file://` XHR requesets](https://emscripten.org/docs/getting_started/FAQ.html#faq-local-webserver)
   
1. Open `http://localhost:8000/` in your browser

---

## 📚 Acknowledgments
- Inspired by [Crafting Interpreters](https://craftinginterpreters.com) by Bob Nystrom.
- Built using modern web technologies and WebAssembly.

---

⭐ **Don't forget to check out the [Live Demo](https://AntonVucinic.github.io/lang)!** ⭐
