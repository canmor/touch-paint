#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <TWMTClient.h>

#include <iostream>
#include <unistd.h> 
#include <complex>
#include <vector>
#include <algorithm>

Display* the_display = NULL;
Window the_main_window;
GC the_main_gc;
XColor the_colors[32];
pthread_mutex_t the_mutex; 
size_t the_drawing_size = 1;
enum WorkingMode {
    Draw,
    Line,
    Dot,
} the_working_mode;

template <typename T>
class Point : public std::complex<T>
{
public:
    Point<T>(T x = static_cast<T>(0), T y = static_cast<T>(0))
        :std::complex<T>(x, y)
    {};
public:
    T x() const { return std::complex<T>::real(); };
    T y() const { return std::complex<T>::imag(); };
    void x(const T& x) { std::complex<T>::real(x); };
    void y(const T& y) { std::complex<T>::imag(y); };
};

template <typename T>
class Shift : public std::complex<T>
{
public:
    Shift<T>(T dx = static_cast<T>(0), T dy = static_cast<T>(0))
        : std::complex<T>(dx, dy)
    {};
    Shift<T>(const std::complex<T>& complex)
        : std::complex<T>(complex)
    {};
public:
    T dx() const { return std::complex<T>::real(); };
    T dy() const { return std::complex<T>::imag(); };
    void dx(const T& dx) { return std::complex<T>::real(dx); };
    void dy(const T& dy) { return std::complex<T>::imag(dy); };
}; 


class Brush
{
public:
    size_t _id;
    std::vector< Point<double> > _loci;
    size_t _step;

public:
    Brush(size_t id, const Point<double>& location)
        : _id(id)
        , _loci(1, location)
        , _step(0)
    {};

public:
    size_t id() const { return _id; };
    void draw(Window win, GC gc, bool from_scratch)
    {
        if (_step >= _loci.size())
            return;

        XWindowAttributes win_attributes = { 0 };
        XGetWindowAttributes(the_display, win, &win_attributes);
        std::vector<XPoint> xpoints;
        typedef std::vector< Point<double> >::const_iterator const_iterator; 
        for (const_iterator it = (!from_scratch && _step > 0) ? _loci.begin() + _step - 1 : _loci.begin(); it != _loci.end(); ++it) {
            XPoint xpoint = { it->x() * win_attributes.width, it->y() * win_attributes.height };
            xpoints.push_back(xpoint);
        }
        _step = from_scratch ? 0 : _loci.size();

        if (xpoints.empty())
            return; 
        XSetForeground(the_display, gc, the_colors[_id % 32].pixel);
        if (the_working_mode == Dot) {
            XDrawPoints(the_display, win, gc, &xpoints[0], xpoints.size(), CoordModeOrigin);
            std::vector<XArc> xarcs;
            for (size_t i = 0; i != xpoints.size(); ++i) {
                XArc xarc = { xpoints[i].x, xpoints[i].y, the_drawing_size, the_drawing_size, 0, 360 * 64 };
                xarcs.push_back(xarc);
            }
            XFillArcs(the_display, win, gc, &xarcs[0], xarcs.size());
        }
        else {
            XSetLineAttributes(the_display, gc, the_drawing_size, LineSolid, CapRound, JoinRound);
            XDrawLines(the_display, win, gc, &xpoints[0], xpoints.size(), CoordModeOrigin);
        }
    };
    
public:
    Brush& operator += (const Brush& other)
    {
        _loci.insert(_loci.end(), other._loci.begin(), other._loci.end());
        return *this;
    };
};

bool operator == (const Brush& left, const Brush& right)
{
    return left.id() == right.id();
};

bool operator != (const Brush& left, const Brush& right)
{
    return left.id() != right.id();
};

typedef struct xinput2_spec {
    Bool is_enabled;
    int opcode;
    int event_base;
    int error_base;
} xinput2_spec_t;

xinput2_spec_t the_xinput2_spec = { 0 };

std::vector<Brush> the_brushes;

void on_touch_message(char* message)
{
    pthread_mutex_lock(&the_mutex);
    char value[256] = { 0 };
    TouchWinSDK::GetValue(message, const_cast<char*>("id"), value);
    size_t id = static_cast<size_t>(-1);
    std::stringstream(value) >> id;
    double x = 0;
    TouchWinSDK::GetValue(message, const_cast<char*>("x"), value);
    std::stringstream(value) >> x;
    double y = 0;
    TouchWinSDK::GetValue(message, const_cast<char*>("y"), value);
    std::stringstream(value) >> y;
    TouchWinSDK::GetValue(message, const_cast<char*>("GestureType"), value);
    std::string phase(value);
    Brush brush(id, Point<double>(x, y));
    if (phase == "GestureUp") {
        std::vector<Brush>::iterator it = std::find(the_brushes.begin(), the_brushes.end(), brush);
        if (it != the_brushes.end())
            the_brushes.erase(it);
    }
    else if (phase == "GestureUpdate") {
        for (std::vector<Brush>::iterator it = the_brushes.begin(); it != the_brushes.end(); ++it) {
            if (*it == brush) {
                *it += brush;
                break;
            }
        }
    }
    else if (phase == "GestureDown") {
        if (the_brushes.empty() && the_working_mode != Draw)
            XClearArea(the_display, the_main_window, 0, 0, 0, 0, true);
        the_brushes.push_back(brush);
    }
    XLockDisplay(the_display);
    XEvent event = { 0 };
    event.type = Expose;
    event.xexpose.window = the_main_window;
    XSendEvent(the_display, the_main_window, False, ExposureMask, &event);
    XFlush(the_display);
    XUnlockDisplay(the_display);
    pthread_mutex_unlock(&the_mutex);
};

void on_map_notify(XEvent* event)
{
    TouchWinSDK::SetCallBackFunc(on_touch_message);
    TouchWinSDK::InitGestureServer();
    TouchWinSDK::ConnectServer();
}

void on_expose(XEvent* event)
{
    if (event->xexpose.count > 0)
        return;

    for (std::vector<Brush>::iterator it = the_brushes.begin(); it != the_brushes.end(); ++it) {
        it->draw(event->xexpose.window, the_main_gc, false);
    }
}

bool on_key_release(XEvent* event)
{
    int key = XLookupKeysym(&event->xkey, 0);
    if (key == XK_Escape) {
        return false;
    }
    else if (key == XK_d) {
        the_working_mode = Draw;
    }
    else if (key == XK_l) {
        the_working_mode = Line;
    }
    else if (key == XK_p) {
        the_working_mode = Dot;
    }
    else if (key == XK_h) {
        ++the_drawing_size;
    }
    else if (key == XK_t) {
        if (the_drawing_size > 1)
            --the_drawing_size;
    }
    else if (key == XK_space)
    {
        std::vector<Brush> empty;
        std::swap(the_brushes, empty);
        XClearArea(the_display, the_main_window, 0, 0, 0, 0, true);
    }
    return true;
}

void on_generic_event(XEvent* event)
{
    bool isXI2Event = false;
    XGenericEventCookie xcookie = { 0 };

    if (XGetEventData(the_display, &xcookie)
        && event->xcookie.extension == the_xinput2_spec.opcode) {
        // remember for later
        isXI2Event = true;
    }
}

bool on_event(Window win, XEvent* event)
{

    switch (event->type) {
    case MapNotify:
        on_map_notify(event);
        break; 
    case Expose:
        on_expose(event);
        break;
    case ButtonPress:
        // fallthrough intended
    case ButtonRelease:
        break;
    case MotionNotify:
        break;
    case KeyPress:
        break;
    case KeyRelease:
        return on_key_release(event);
    case PropertyNotify:
        break;
    case EnterNotify:
    case LeaveNotify:
        break;
    case SelectionClear:
        break;
    case GenericEvent:
        on_generic_event(event);
        break;
    case ClientMessage:
        return false;
    default:
        break;
    } 
    return true;
};

void set_fullscreen(Display* display, Window window)
{
    Atom wm_state = XInternAtom(the_display, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(the_display, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent event = { 0 };
    event.type = ClientMessage;
    event.xclient.window = the_main_window;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = fullscreen;
    event.xclient.data.l[2] = 0;
    XSendEvent(the_display, DefaultRootWindow(the_display), False, SubstructureNotifyMask, &event);
};

int main(int argc, char* argv[])
{
    pthread_mutex_init(&the_mutex, NULL);
    XInitThreads();

    the_display = XOpenDisplay(NULL);

    int screen = DefaultScreen(the_display);
    int width = DisplayWidth(the_display, screen);
    int height = DisplayHeight(the_display, screen); 
    int black_pixel = BlackPixel(the_display, screen);
    int white_pixel = WhitePixel(the_display, screen);

    the_xinput2_spec.is_enabled = XQueryExtension(the_display, "XInputExtension",
        &the_xinput2_spec.opcode, &the_xinput2_spec.event_base, &the_xinput2_spec.error_base);
    std::cout << "with XInput 2 support: "
        << (the_xinput2_spec.is_enabled ? "true" : "false") << std::endl;

    Window root = DefaultRootWindow(the_display);
    the_main_window = XCreateSimpleWindow(the_display, root,
        0, 0, width >> 1, height >> 1,
        3, black_pixel, white_pixel);

    long event_mask =
        KeymapStateMask | EnterWindowMask | LeaveWindowMask | PropertyChangeMask
        | PointerMotionMask | PointerMotionHintMask | Button1MotionMask
        | KeyPressMask | KeyReleaseMask
        | ButtonPressMask | StructureNotifyMask | ExposureMask;
    XSelectInput(the_display, the_main_window, event_mask);

    XMapWindow(the_display, the_main_window);

    the_main_gc = XCreateGC(the_display, the_main_window, 0, NULL); 

    Colormap colormap = DefaultColormap(the_display, DefaultScreen(the_display));
    for (size_t i = 0; i < 32; ++i) {
        XColor color;
        color.red = rand() % 0xffff;
        color.green = rand() % 0xffff;
        color.blue = rand() % 0xffff;
        Status rc = XAllocColor(the_display, colormap, &color);
        the_colors[i] = color;
    }

    set_fullscreen(the_display, the_main_window);

    XEvent event = { 0 };
    bool continued = true;
    while (continued) {
        pthread_mutex_lock(&the_mutex);
        XLockDisplay(the_display);
        if (XPending(the_display) <= 0) {
            XUnlockDisplay(the_display);
            pthread_mutex_unlock(&the_mutex);
            usleep(5000);
            continue;
        } 
        continued = XNextEvent(the_display, &event) >= 0 && on_event(the_main_window, &event);
        XUnlockDisplay(the_display);
        pthread_mutex_unlock(&the_mutex);
    }

    pthread_mutex_destroy(&the_mutex);
    XFreeGC(the_display, the_main_gc);
    XDestroyWindow(the_display, the_main_window);
    XCloseDisplay(the_display);

    return 0;
}

