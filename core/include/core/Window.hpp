#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;

namespace core {
    enum class EventType {
        None = 0,
        WindowClose, WindowResize,
        KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    struct Event {
        EventType type = EventType::None;
        bool handled = false;
        virtual ~Event() = default;
    };

    struct WindowResizeEvent : public Event {
        uint32_t width, height;
        WindowResizeEvent(uint32_t w, uint32_t h) : width(w), height(h) { type = EventType::WindowResize; }
    };

    struct WindowCloseEvent : public Event {
        WindowCloseEvent() { type = EventType::WindowClose; }
    };

    class Window {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        Window(const std::string& title, uint32_t width, uint32_t height, bool borderless = false);
        ~Window();

        // コピー・ムーブ禁止（シングルトン的な扱いを強制）
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // --- MUST (必須要件) ---
        bool shouldClose() const;
        void pollEvents();
        uint32_t getWidth() const { return m_width; }
        uint32_t getHeight() const { return m_height; }
        
        // RHI層へ渡すVulkan連携機能
        static std::vector<const char*> getRequiredVulkanExtensions();
        GLFWwindow* getNativeHandle() const { return m_window; }

        // --- SHOULD (イベント駆動: バケツリレーの口) ---
        void setEventCallback(const EventCallbackFn& callback) { m_data.eventCallback = callback; }

        // --- SHOULD (入力ポーリング) ---
        bool isKeyPressed(int keycode) const;
        bool isMouseButtonPressed(int button) const;
        std::pair<float, float> getMousePosition() const;

        // --- WANT (拡張要件) ---
        void setCursorMode(bool visible);
        void setClipboardText(const char* text);
        const char* getClipboardText() const;

    private:
        void init(const std::string& title, bool borderless);
        void shutdown();

    private:
        GLFWwindow* m_window = nullptr;
        uint32_t m_width;
        uint32_t m_height;

        // GLFWコールバックへ渡すための内部データ構造体
        struct WindowData {
            std::string title;
            uint32_t width, height;
            EventCallbackFn eventCallback;
        };
        WindowData m_data;
    };

}
