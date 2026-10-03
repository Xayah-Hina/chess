export module chess.task;
import std;

export namespace chess {
    struct TaskContext final {
        inline static thread_local std::chrono::steady_clock::time_point until = std::chrono::steady_clock::time_point::max();
        inline static thread_local std::coroutine_handle<> suspended, ready;
        inline static thread_local bool adjudicating{};
        inline static thread_local std::uint64_t rule_nodes{};
    };
    struct Yield final {
        bool await_ready() const {
            return std::chrono::steady_clock::now() < TaskContext::until;
        }
        void await_suspend(std::coroutine_handle<> handle) const {
            TaskContext::suspended = handle;
        }
        void await_resume() const {}
    };
    template <class T>
    struct Task final {
        struct promise_type final {
            std::optional<T> value;
            std::exception_ptr error;
            std::coroutine_handle<> continuation;
            Task get_return_object() {
                return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_always initial_suspend() const noexcept {
                return {};
            }
            struct Finish final {
                bool await_ready() const noexcept {
                    return false;
                }
                void await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
                    TaskContext::ready = handle.promise().continuation;
                }
                void await_resume() const noexcept {}
            };
            Finish final_suspend() const noexcept {
                return {};
            }
            void return_value(T result) {
                value = std::move(result);
            }
            void unhandled_exception() {
                error = std::current_exception();
            }
        };
        std::coroutine_handle<promise_type> handle;
        std::coroutine_handle<> next;
        explicit Task(std::coroutine_handle<promise_type> frame) : handle{frame}, next{frame} {}
        Task(Task&& other) noexcept : handle{std::exchange(other.handle, {})}, next{other.next} {}
        Task(const Task&) = delete;
        ~Task() {
            if (handle) handle.destroy();
        }
        bool resume() {
            TaskContext::ready = next;
            while (TaskContext::ready) {
                const auto current = std::exchange(TaskContext::ready, {});
                current.resume();
            }
            if (!handle.done()) {
                next = TaskContext::suspended;
                return false;
            }
            if (handle.promise().error) std::rethrow_exception(handle.promise().error);
            return true;
        }
        struct Await final {
            std::coroutine_handle<promise_type> handle;
            bool await_ready() const noexcept {
                return false;
            }
            void await_suspend(std::coroutine_handle<> caller) const noexcept {
                handle.promise().continuation = caller;
                TaskContext::ready            = handle;
            }
            T await_resume() const {
                if (handle.promise().error) std::rethrow_exception(handle.promise().error);
                return std::move(*handle.promise().value);
            }
        };
        Await operator co_await() const {
            return {handle};
        }
    };
} // namespace chess
