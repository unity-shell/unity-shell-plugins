#include "gsettings-glib-source.hpp"

namespace wfgs
{
/* Bridge GLib dispatching into the wl_event_loop used by Wayfire. */
glib_source_t::glib_source_t(wl_event_loop *loop) : wl(loop)
{
    ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    g_main_context_acquire(ctx);

    timer = wl_event_loop_add_timer(wl, &glib_source_t::on_timeout, this);
    resync();
}

glib_source_t::~glib_source_t()
{
    for (auto *src : fd_sources)
    {
        wl_event_source_remove(src);
    }

    if (timer)
    {
        wl_event_source_remove(timer);
    }

    g_main_context_release(ctx);
    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);
}

void glib_source_t::resync()
{
    g_main_context_prepare(ctx, &max_priority);

    gint timeout = -1;
    gint n;
    if (pollfds.size() < 8)
    {
        pollfds.resize(8);
    }

    while ((n = g_main_context_query(ctx, max_priority, &timeout,
        pollfds.data(), static_cast<gint>(pollfds.size()))) > static_cast<gint>(pollfds.size()))
    {
        pollfds.resize(n);
    }

    pollfds.resize(n);

    for (auto *src : fd_sources)
    {
        wl_event_source_remove(src);
    }

    fd_sources.clear();

    for (auto& pfd : pollfds)
    {
        pfd.revents = 0;
        uint32_t mask = 0;
        if (pfd.events & G_IO_IN)
        {
            mask |= WL_EVENT_READABLE;
        }

        if (pfd.events & G_IO_OUT)
        {
            mask |= WL_EVENT_WRITABLE;
        }

        fd_sources.push_back(
            wl_event_loop_add_fd(wl, pfd.fd, mask, &glib_source_t::on_fd, this));
    }

    wl_event_source_timer_update(timer, timeout < 0 ? 0 : (timeout == 0 ? 1 : timeout));
}

int glib_source_t::on_fd(int fd, uint32_t mask, void *data)
{
    auto *self = static_cast<glib_source_t*>(data);
    for (auto& pfd : self->pollfds)
    {
        if (pfd.fd != fd)
        {
            continue;
        }

        if (mask & WL_EVENT_READABLE)
        {
            pfd.revents |= G_IO_IN;
        }

        if (mask & WL_EVENT_WRITABLE)
        {
            pfd.revents |= G_IO_OUT;
        }

        if (mask & WL_EVENT_ERROR)
        {
            pfd.revents |= G_IO_ERR;
        }

        if (mask & WL_EVENT_HANGUP)
        {
            pfd.revents |= G_IO_HUP;
        }
    }

    if (!self->dispatch_queued)
    {
        self->dispatch_queued = true;
        wl_event_loop_add_idle(self->wl, &glib_source_t::on_idle, self);
    }

    return 0;
}

int glib_source_t::on_timeout(void *data)
{
    static_cast<glib_source_t*>(data)->dispatch();
    return 0;
}

void glib_source_t::on_idle(void *data)
{
    auto *self = static_cast<glib_source_t*>(data);
    self->dispatch_queued = false;
    self->dispatch();
}

void glib_source_t::dispatch()
{
    g_main_context_check(ctx, max_priority, pollfds.data(), static_cast<gint>(pollfds.size()));
    g_main_context_dispatch(ctx);
    resync();
}
}
