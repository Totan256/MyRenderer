#include "core/Window.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>

namespace core {

    // 複数ウィンドウを作成した場合でもGLFWの初期化・破棄を正しく行うためのカウンタ
    static int s_GLFWWindowCount = 0;

    static void GLFWErrorCallback(int error, const char* description) {
        std::cerr << "[GLFW Error] (" << error << "): " << description << std::endl;
    }

    Window::Window(const std::string& title, uint32_t width, uint32_t height, bool borderless)
        : m_width(width), m_height(height) 
    {
        init(title, borderless);
    }

    Window::~Window() {
        shutdown();
    }

    void Window::init(const std::string& title, bool borderless) {
        m_data.title = title;
        m_data.width = m_width;
        m_data.height = m_height;

        // 初回のウィンドウ作成時のみGLFWを初期化
        if (s_GLFWWindowCount == 0) {
            int success = glfwInit();
            if (!success) {
                throw std::runtime_error("Failed to initialize GLFW!");
            }
            glfwSetErrorCallback(GLFWErrorCallback);
        }

        // Vulkanを使用するため、OpenGLコンテキストは作成しないよう指定 (必須)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        
        // ボーダーレス（枠なし）の設定
        if (borderless) {
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        }

        m_window = glfwCreateWindow((int)m_width, (int)m_height, m_data.title.c_str(), nullptr, nullptr);
        if (!m_window) {
            throw std::runtime_error("Failed to create GLFW window!");
        }

        s_GLFWWindowCount++;

        // GLFWのコールバックでWindowData構造体を引き出せるようにポインタを登録
        glfwSetWindowUserPointer(m_window, &m_data);

        // ==========================================================
        // GLFW コールバックの設定 (イベント発火)
        // ==========================================================
        
        // リサイズイベント
        glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
            WindowData* data = (WindowData*)glfwGetWindowUserPointer(window);
            data->width = width;
            data->height = height;

            if (data->eventCallback) {
                WindowResizeEvent event(width, height);
                data->eventCallback(event);
            }
        });

        // 閉じるイベント
        glfwSetWindowCloseCallback(m_window, [](GLFWwindow* window) {
            WindowData* data = (WindowData*)glfwGetWindowUserPointer(window);
            if (data->eventCallback) {
                WindowCloseEvent event;
                data->eventCallback(event);
            }
        });
        
        // ※今後、必要に応じてキーボードやマウスのコールバックもここに追加していきます
    }

    void Window::shutdown() {
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
            s_GLFWWindowCount--;

            // 最後のウィンドウが破棄されたらGLFWを終了
            if (s_GLFWWindowCount == 0) {
                glfwTerminate();
            }
        }
    }

    // --- MUST ---
    bool Window::shouldClose() const {
        return glfwWindowShouldClose(m_window);
    }

    void Window::pollEvents() {
        glfwPollEvents();
    }

    std::vector<const char*> Window::getRequiredVulkanExtensions() const {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        
        if (glfwExtensions == nullptr) {
            throw std::runtime_error("GLFW failed to return required Vulkan extensions!");
        }

        return std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    // --- SHOULD (入力ポーリング) ---
    bool Window::isKeyPressed(int keycode) const {
        auto state = glfwGetKey(m_window, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Window::isMouseButtonPressed(int button) const {
        auto state = glfwGetMouseButton(m_window, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> Window::getMousePosition() const {
        double xpos, ypos;
        glfwGetCursorPos(m_window, &xpos, &ypos);
        return { static_cast<float>(xpos), static_cast<float>(ypos) };
    }

    // --- WANT (拡張機能) ---
    void Window::setCursorMode(bool visible) {
        glfwSetInputMode(m_window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }

    void Window::setClipboardText(const char* text) {
        glfwSetClipboardString(m_window, text);
    }

    const char* Window::getClipboardText() const {
        return glfwGetClipboardString(m_window);
    }

} // namespace core