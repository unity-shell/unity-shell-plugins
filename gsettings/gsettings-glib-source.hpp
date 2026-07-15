#pragma once

#include <glib.h>
#include <wayland-server-core.h>

#include <vector>

namespace wfgs
{
class glib_source_t
{
  public:
    explicit glib_source_t(wl_event_loop *loop);
    ~glib_source_t();

    glib_source_t(const glib_source_t&) = delete;
    glib_source_t& operator =(const glib_source_t&) = delete;

    GMainContext *context() const
    {
        return ctx;
    }

  private:
    void resync();
    void dispatch();

    static int on_fd(int fd, uint32_t mask, void *data);
    static int on_timeout(void *data);
    static void on_idle(void *data);

    wl_event_loop *wl;
    GMainContext  *ctx;
    gint max_priority = 0;

    std::vector<GPollFD> pollfds;
    std::vector<wl_event_source*> fd_sources;
    wl_event_source *timer = nullptr;
    bool dispatch_queued = false;
};
}
