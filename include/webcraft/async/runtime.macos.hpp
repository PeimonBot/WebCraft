#pragma once

#ifdef __APPLE__

#include <webcraft/async/runtime.hpp>
#include <sys/event.h>
#include <unistd.h>
#include <utility>
#include <concepts>
#include <chrono>
#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <atomic>
#include <stdexec/execution.hpp>

namespace webcraft::async::runtime
{
    class runtime_context;
    class kqueue_scheduler;
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    namespace detail
    {
        struct env
        {
            kqueue_scheduler *sched_;
        };

        struct schedule_sender
        {
            int queue_;
            kqueue_scheduler *sched_;

            using sender_concept = stdexec::sender_t;

            using completion_signatures = stdexec::completion_signatures<
                stdexec::set_value_t(),
                stdexec::set_error_t(std::exception_ptr),
                stdexec::set_stopped_t()>;

            template <class Receiver>
            struct operation : public webcraft::async::runtime::runtime_event
            {
                int queue_;
                Receiver receiver_;

                void start() noexcept
                {
                    try
                    {
                        // Post a NOP event to the kqueue
                        uint64_t id = reinterpret_cast<uint64_t>(this);
                        struct kevent event;
                        EV_SET(&event, id, EVFILT_USER, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                        int result = kevent(queue_, &event, 1, nullptr, 0, nullptr);
                        if (result < 0)
                        {
                            throw std::runtime_error("Failed to register event to kqueue");
                        }

                        // Trigger the event
                        EV_SET(&event, id, EVFILT_USER, 0, NOTE_TRIGGER, 0, this);
                        result = kevent(queue_, &event, 1, nullptr, 0, nullptr);
                        if (result < 0)
                        {
                            throw std::runtime_error("Failed to trigger event in kqueue");
                        }
                    }
                    catch (...)
                    {
                        stdexec::set_error(std::move(receiver_), std::current_exception());
                    }
                }

                void execute_callback() override
                {
                    stdexec::set_value(std::move(receiver_));
                }
            };

            template <class Receiver>
            auto connect(Receiver &&receiver) const -> operation<std::remove_cvref_t<Receiver>>
            {
                return {queue_, std::forward<Receiver>(receiver)};
            }
        };

        inline constexpr env tag_invoke(stdexec::get_env_t, const schedule_sender &s) noexcept
        {
            return env{.sched_ = const_cast<kqueue_scheduler *>(s.sched_)};
        }

        inline constexpr auto tag_invoke(stdexec::get_completion_scheduler<stdexec::set_value_t>, const env &env) noexcept -> kqueue_scheduler
        {
            return *env.sched_;
        }

        static_assert(stdexec::sender<schedule_sender>, "schedule_sender is not a sender");

        // struct schedule_after_sender
        // {
        //     int queue_;
        //     duration dur_;
        //     kqueue_scheduler *sched_;

        //     using sender_concept = stdexec::sender_t;

        //     using completion_signatures = stdexec::completion_signatures<
        //         stdexec::set_value_t(),
        //         stdexec::set_error_t(std::exception_ptr),
        //         stdexec::set_stopped_t()>;

        //     template <class Receiver>
        //     struct operation;

        //     template <class Receiver>
        //     auto connect(Receiver &&receiver) const -> operation<std::remove_cvref_t<Receiver>>
        //     {
        //         return {queue_, dur_, std::forward<Receiver>(receiver), 0, sched_};
        //     }

        //     friend auto tag_invoke(stdexec::get_env_t, const schedule_after_sender &s) noexcept
        //     {
        //         return env{.sched_ = s.sched_};
        //     }

        // };

    }

    class kqueue_scheduler
    {
    private:
        int queue_;
        // std::atomic<uint64_t> next_timer_id_{1};
        // std::mutex timer_mutex_;
        // std::map<uint64_t, std::unique_ptr<runtime_event>> timer_events_;

        friend class runtime_context;
        // friend class detail::schedule_after_sender;

        explicit kqueue_scheduler(int queue) noexcept : queue_(queue) {}

        // void register_timer_event(uint64_t id, std::unique_ptr<runtime_event> event)
        // {
        //     std::lock_guard<std::mutex> lock(timer_mutex_);
        //     timer_events_[id] = std::move(event);
        // }

        // void remove_timer_event(uint64_t id)
        // {
        //     std::lock_guard<std::mutex> lock(timer_mutex_);
        //     timer_events_.erase(id);
        // }

        // std::unique_ptr<runtime_event> extract_timer_event(uint64_t id)
        // {
        //     std::lock_guard<std::mutex> lock(timer_mutex_);
        //     auto it = timer_events_.find(id);
        //     if (it == timer_events_.end())
        //     {
        //         return nullptr;
        //     }
        //     auto event = std::move(it->second);
        //     timer_events_.erase(it);
        //     return event;
        // }

    public:
        // Required by the scheduler concept
        auto schedule() const noexcept
        {
            return detail::schedule_sender{queue_, const_cast<kqueue_scheduler *>(this)};
        }

        // Required by the timed_scheduler concept
        // time_point now() const noexcept
        // {
        //     return std::chrono::steady_clock::now();
        // }

        // // Schedule after a duration
        // auto schedule_after(duration dur) const noexcept
        // {

        //     return detail::schedule_after_sender{queue_, dur, const_cast<kqueue_scheduler *>(this)};
        // }

        // // Schedule at a specific time point
        // auto schedule_at(time_point tp) const noexcept
        // {
        //     return schedule_after(tp - now());
        // }

        // // Helper function to generate a random uint64_t
        // static uint64_t generate_random_uint64()
        // {
        //     static std::atomic<uint64_t> counter{1};
        //     return counter++;
        // }

        // auto query(stdexec::get_forward_progress_guarantee_t) const noexcept
        //     -> stdexec::forward_progress_guarantee
        // {
        //     return stdexec::forward_progress_guarantee::parallel;
        // }

        // auto query(stdexec::execute_may_block_caller_t) const noexcept -> bool
        // {
        //     return false;
        // }

        // auto operator==(const kqueue_scheduler &t) const noexcept -> bool
        // {
        //     return t.queue_ == queue_;
        // }
    };

    // namespace detail
    // {
    //     template <class Receiver>
    //     struct schedule_after_sender::operation : public runtime_event
    //     {
    //         int queue_;
    //         duration dur_;
    //         Receiver receiver_;
    //         uint64_t timer_id_;
    //         kqueue_scheduler *scheduler_;

    //         void start() noexcept
    //         {
    //             try
    //             {
    //                 timer_id_ = reinterpret_cast<uint64_t>(this);

    //                 // Create a timer event
    //                 struct kevent event;
    //                 auto ns_count = std::chrono::duration_cast<std::chrono::nanoseconds>(dur_).count();

    //                 // Register the timer with kqueue
    //                 EV_SET(&event, timer_id_, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS, ns_count, this);
    //                 int result = kevent(queue_, &event, 1, nullptr, 0, nullptr);
    //                 if (result < 0)
    //                 {
    //                     throw std::runtime_error("Failed to register timer event to kqueue");
    //                 }

    //                 // Store the operation in the scheduler
    //                 scheduler_->register_timer_event(timer_id_, std::unique_ptr<runtime_event>(this));
    //             }
    //             catch (...)
    //             {
    //                 stdexec::set_error(std::move(receiver_), std::current_exception());
    //             }
    //         }

    //         void execute_callback()
    //         {
    //             scheduler_->remove_timer_event(timer_id_);
    //             stdexec::set_value(std::move(receiver_));
    //         }
    //     };

    //     static_assert(stdexec::sender<schedule_after_sender>, "schedule_after_sender is not a sender");
    // }

    // Tag invoke overloads for the timed_scheduler concept
    // template <typename Scheduler>
    // auto tag_invoke(exec::now_t, const Scheduler &scheduler) noexcept
    //     -> std::enable_if_t<std::is_same_v<Scheduler, kqueue_scheduler>, typename Scheduler::time_point>
    // {
    //     return scheduler.now();
    // }

    // template <typename Scheduler>
    // auto tag_invoke(exec::schedule_after_t, Scheduler scheduler, const typename Scheduler::duration &dur) noexcept
    //     -> std::enable_if_t<std::is_same_v<Scheduler, kqueue_scheduler>, decltype(scheduler.schedule_after(dur))>
    // {
    //     return scheduler.schedule_after(dur);
    // }

    // template <typename Scheduler>
    // auto tag_invoke(exec::schedule_at_t, Scheduler scheduler, const typename Scheduler::time_point &tp) noexcept
    //     -> std::enable_if_t<std::is_same_v<Scheduler, kqueue_scheduler>, decltype(scheduler.schedule_at(tp))>
    // {
    //     return scheduler.schedule_at(tp);
    // }

    static_assert(stdexec::scheduler<kqueue_scheduler>, "kqueue_scheduler does not satisfy scheduler concept");
    // static_assert(exec::timed_scheduler<kqueue_scheduler>, "kqueue_scheduler does not satisfy timed_scheduler concept");

    class runtime_context
    {
    private:
        int queue;
        std::atomic<bool> running_{true};

    public:
        runtime_context() noexcept
        {
            // Create the kqueue
            queue = kqueue();
            if (queue == -1)
            {
                throw std::runtime_error("Failed to create kqueue");
            }
        }

        ~runtime_context() noexcept
        {
            // Close the kqueue
            if (queue != -1)
            {
                close(queue);
            }
        }

        kqueue_scheduler get_scheduler() noexcept
        {
            return kqueue_scheduler{queue};
        }

        int get_native_handle() noexcept
        {
            return queue;
        }

        void run() noexcept
        {
            while (running_)
            {
                struct kevent event;
                // Wait for events with a timeout of 100ms to allow checking the running_ flag
                struct timespec timeout = {0, 100000000}; // 100ms
                int result = kevent(queue, nullptr, 0, &event, 1, &timeout);

                if (result > 0)
                {
                    // Process the event
                    if (event.filter == EVFILT_USER || event.filter == EVFILT_TIMER)
                    {
                        auto *event_ptr = static_cast<runtime_event *>(event.udata);
                        if (event_ptr)
                        {
                            event_ptr->execute_callback();
                        }

                        // For timer events, we need to remove the event
                        if (event.filter == EVFILT_TIMER)
                        {
                            // Remove the timer event
                            EV_SET(&event, event.ident, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
                            kevent(queue, &event, 1, nullptr, 0, nullptr);
                        }
                    }
                }
            }
        }

        void finish() noexcept
        {
            running_ = false;
        }
    };

    // static_assert(runtime_context_trait<runtime_context>, "runtime_concext does not satisfy runtime_context_trait concept");
    // static_assert(std::is_same_v<decltype(std::declval<runtime_context>().get_scheduler()), kqueue_scheduler>,
    //               "runtime_context::get_scheduler() does not return kqueue_scheduler");
    // static_assert(std::is_same_v<native_runtime_handle<runtime_context>, int>,
    //               "native_runtime_handle<runtime_context> is not int");
}

#endif

// Made with Bob
