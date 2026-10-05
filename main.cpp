#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cmath>
#include <iostream>
#include <vector>
#include <utility>
#include <sys/time.h>
#include <unistd.h>

using namespace std;

/*
    Assignment 3 - 3D Floating Shape

    The program demonstrates a basic software 3D renderer.

    Rendering pipeline:
        3D world coordinates
            -> perspective projection
            -> 2D screen coordinates
            -> framebuffer
            -> X11 window

    The shapes are centered around the world origin so that rotations
    around the Y and Z axes happen around the shape's center.

    Source/reference:
    Assignment 3 class demonstration: 3D-floating-shape.
*/

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------

const int WIDTH = 800;
const int HEIGHT = 600;

const double FOCAL_LENGTH = 600.0;

// 16,667 microseconds is approximately 60 frames per second.
const long long FRAME_TIME_US = 16667;

// ------------------------------------------------------------
// 3D data structures
// ------------------------------------------------------------

struct point_3d_t {
    double x;
    double y;
    double z;
};

struct shape_3d_t {
    vector<point_3d_t> points;
    vector<pair<int, int>> edges;
};

// ------------------------------------------------------------
// Shapes
// ------------------------------------------------------------

// Cube centered at (0,0,0).
shape_3d_t cube = {
    {
        {-5,  5,  5},
        { 5,  5,  5},
        { 5, -5,  5},
        {-5, -5,  5},

        {-5,  5, -5},
        { 5,  5, -5},
        { 5, -5, -5},
        {-5, -5, -5}
    },

    {
        {0,1},
        {1,2},
        {2,3},
        {3,0},

        {4,5},
        {5,6},
        {6,7},
        {7,4},

        {0,4},
        {1,5},
        {2,6},
        {3,7}
    }
};

// Square-base pyramid centered approximately around the origin.
shape_3d_t square_pyramid = {
    {
        { 0,  6,  0 },
        {-5, -5,  5},
        { 5, -5,  5},
        { 5, -5, -5},
        {-5, -5, -5}
    },

    {
        {0,1},
        {0,2},
        {0,3},
        {0,4},

        {1,2},
        {2,3},
        {3,4},
        {4,1}
    }
};

// Triangle-based prism centered around the origin.
shape_3d_t triangular_prism = {
    {
        { 0,  5,  5},
        {-5, -5,  5},
        { 5, -5,  5},

        { 0,  5, -5},
        {-5, -5, -5},
        { 5, -5, -5}
    },

    {
        {0,1},
        {1,2},
        {2,0},

        {3,4},
        {4,5},
        {5,3},

        {0,3},
        {1,4},
        {2,5}
    }
};

vector<shape_3d_t> shapes_list = {
    cube,
    triangular_prism,
    square_pyramid
};

// ------------------------------------------------------------
// World coordinate axes
// ------------------------------------------------------------

// The demonstrated program uses axes extending approximately
// 50 world units in each direction.
vector<point_3d_t> axis_points = {
    {-50, 0, 0},
    { 50, 0, 0},

    {0, -50, 0},
    {0,  50, 0},

    {0, 0, -50},
    {0, 0,  50}
};

vector<pair<int, int>> axis_edges = {
    {0,1},
    {2,3},
    {4,5}
};

// ------------------------------------------------------------
// Timing
// ------------------------------------------------------------

long long currentTimeMicros() {
    timeval tv;

    gettimeofday(&tv, nullptr);

    return static_cast<long long>(tv.tv_sec) * 1000000LL
         + static_cast<long long>(tv.tv_usec);
}

// ------------------------------------------------------------
// Perspective projection
// ------------------------------------------------------------

/*
    Convert a 3D point into a 2D screen position.

    First we move the point into camera-relative coordinates.

    Then the view is rotated around the Y axis.

    Finally, perspective projection divides by depth.

    The division by depth is what makes closer objects appear larger.

    The screen center is approximately (400,300).
*/
point_3d_t project(
    const point_3d_t& point,
    const point_3d_t& camera,
    double view_angle
) {
    // Move from world coordinates into camera-relative coordinates.
    double x = point.x - camera.x;
    double y = point.y - camera.y;
    double z = point.z - camera.z;

    // Rotate the view around the Y axis.
    double rotated_x =
        cos(view_angle) * x -
        sin(view_angle) * z;

    double rotated_z =
        sin(view_angle) * x +
        cos(view_angle) * z;

    /*
        The camera is looking toward decreasing Z.

        Therefore the useful depth is the negative of rotated_z.
    */
    double depth = -rotated_z;

    // Do not attempt perspective division at or behind the camera.
    if (depth <= 0.0001) {
        return {0, 0, 0};
    }

    // Perspective projection.
    double screen_x =
        FOCAL_LENGTH * rotated_x / depth
        + WIDTH / 2.0;

    double screen_y =
        -FOCAL_LENGTH * y / depth
        + HEIGHT / 2.0;

    return {
        screen_x,
        screen_y,
        1
    };
}

// ------------------------------------------------------------
// Draw a line into the framebuffer
// ------------------------------------------------------------

void drawLine(
    unsigned int* framebuffer,
    int x0,
    int y0,
    int x1,
    int y1,
    unsigned int color
) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;

    int error = dx - dy;

    while (true) {
        // Only write pixels that are actually inside the window.
        if (
            x0 >= 0 &&
            x0 < WIDTH &&
            y0 >= 0 &&
            y0 < HEIGHT
        ) {
            framebuffer[y0 * WIDTH + x0] = color;
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int e2 = 2 * error;

        if (e2 > -dy) {
            error -= dy;
            x0 += sx;
        }

        if (e2 < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

// ------------------------------------------------------------
// Draw world axes
// ------------------------------------------------------------

void drawWorldAxes(
    unsigned int* framebuffer,
    const point_3d_t& camera,
    double view_angle
) {
    for (const auto& edge : axis_edges) {
        point_3d_t p1 = project(
            axis_points[edge.first],
            camera,
            view_angle
        );

        point_3d_t p2 = project(
            axis_points[edge.second],
            camera,
            view_angle
        );

        // Gray axes match the basic demonstrated renderer.
        drawLine(
            framebuffer,
            static_cast<int>(p1.x),
            static_cast<int>(p1.y),
            static_cast<int>(p2.x),
            static_cast<int>(p2.y),
            0xff676767
        );
    }
}

// ------------------------------------------------------------
// Rotate one point around Y
// ------------------------------------------------------------

point_3d_t rotate_point_around_y(
    const point_3d_t& point,
    double angle
) {
    double new_x =
        cos(angle) * point.x -
        sin(angle) * point.z;

    double new_z =
        sin(angle) * point.x +
        cos(angle) * point.z;

    // Rotation around Y does not change Y.
    return {
        new_x,
        point.y,
        new_z
    };
}

// ------------------------------------------------------------
// Rotate one point around Z
// ------------------------------------------------------------

point_3d_t rotate_point_around_z(
    const point_3d_t& point,
    double angle
) {
    double new_x =
        cos(angle) * point.x -
        sin(angle) * point.y;

    double new_y =
        sin(angle) * point.x +
        cos(angle) * point.y;

    // Rotation around Z does not change Z.
    return {
        new_x,
        new_y,
        point.z
    };
}

// ------------------------------------------------------------
// Rotate an entire shape around Y
// ------------------------------------------------------------

vector<point_3d_t> rotate_shape_around_y(
    const vector<point_3d_t>& points,
    double angle
) {
    vector<point_3d_t> rotated_points;

    for (const point_3d_t& point : points) {
        rotated_points.push_back(
            rotate_point_around_y(point, angle)
        );
    }

    return rotated_points;
}

// ------------------------------------------------------------
// Rotate an entire shape around Z
// ------------------------------------------------------------

vector<point_3d_t> rotate_shape_around_z(
    const vector<point_3d_t>& points,
    double angle
) {
    vector<point_3d_t> rotated_points;

    for (const point_3d_t& point : points) {
        rotated_points.push_back(
            rotate_point_around_z(point, angle)
        );
    }

    return rotated_points;
}

// ------------------------------------------------------------
// Draw a shape
// ------------------------------------------------------------

void drawShape(
    const shape_3d_t& shape,
    unsigned int* framebuffer,
    const point_3d_t& camera,
    double view_angle
) {
    /*
        The demonstrated renderer draws the edges directly after
        projecting their endpoints into screen coordinates.
    */

    unsigned int colors[3] = {
        0xffff0000, // red
        0xff00ff00, // green
        0xff0000ff  // blue
    };

    int color_index = 0;

    for (const auto& edge : shape.edges) {
        point_3d_t p1 = project(
            shape.points[edge.first],
            camera,
            view_angle
        );

        point_3d_t p2 = project(
            shape.points[edge.second],
            camera,
            view_angle
        );

        // Ignore an edge if one of its points cannot be projected.
        if (p1.z == 0 || p2.z == 0) {
            continue;
        }

        drawLine(
            framebuffer,
            static_cast<int>(p1.x),
            static_cast<int>(p1.y),
            static_cast<int>(p2.x),
            static_cast<int>(p2.y),
            colors[color_index]
        );

        color_index++;

        if (color_index == 3) {
            color_index = 0;
        }
    }
}

// ------------------------------------------------------------
// Keyboard input
// ------------------------------------------------------------

bool handleUserInput(
    Display* display,
    int& current_shape,
    bool& show_axes,
    double& view_angle
) {
    while (XPending(display)) {
        XEvent event;

        XNextEvent(display, &event);

        if (event.type != KeyPress) {
            continue;
        }

        KeySym key = XLookupKeysym(
            &event.xkey,
            0
        );

        // Escape closes the program.
        if (key == XK_Escape) {
            return false;
        }

        // X/x toggles the coordinate axes.
        if (key == XK_x) {
            show_axes = !show_axes;
        }

        // S/s cycles through the three shapes.
        if (key == XK_s) {
            current_shape =
                (current_shape + 1)
                % static_cast<int>(shapes_list.size());
        }

        // Left/right changes the viewing angle.
        if (key == XK_Left) {
            view_angle -= 0.05;
        }

        if (key == XK_Right) {
            view_angle += 0.05;
        }
    }

    return true;
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main() {
    cout << "Software Renderer Starting!" << endl;

    // Connect to the X11 display.
    Display* display = XOpenDisplay(nullptr);

    if (display == nullptr) {
        cerr << "Could not open X11 display! Exiting..." << endl;
        return 1;
    }

    int screen = DefaultScreen(display);

    // Create the 800x600 X11 window.
    Window window = XCreateSimpleWindow(
        display,
        RootWindow(display, screen),
        100,
        100,
        WIDTH,
        HEIGHT,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    /*
        KeyPressMask is required because the program needs to respond
        to keyboard controls such as X, S, and the arrow keys.
    */
    XSelectInput(
        display,
        window,
        ExposureMask | KeyPressMask
    );

    XMapWindow(display, window);

    GC gc = XCreateGC(
        display,
        window,
        0,
        nullptr
    );

    /*
        The framebuffer contains one unsigned integer for every pixel.

        800 * 600 = 480,000 pixels.
    */
    vector<unsigned int> framebuffer(
        WIDTH * HEIGHT,
        0xffffffff
    );

    /*
        XImage allows the framebuffer's pixel data to be displayed
        inside the X11 window.
    */
    XImage* image = XCreateImage(
        display,
        DefaultVisual(display, screen),
        DefaultDepth(display, screen),
        ZPixmap,
        0,
        reinterpret_cast<char*>(framebuffer.data()),
        WIDTH,
        HEIGHT,
        32,
        0
    );

    if (image == nullptr) {
        cerr << "Could not create XImage. Exiting..." << endl;

        XFreeGC(display, gc);
        XDestroyWindow(display, window);
        XCloseDisplay(display);

        return 1;
    }

    // Camera position from the demonstrated renderer.
    point_3d_t camera = {
        5.0,
        7.0,
        70.0
    };

    // Start with coordinate axes visible.
    bool show_axes = true;

    // Start with the first shape.
    int current_shape = 0;

    // Rotation angles.
    double y_rotation = 0.0;
    double z_rotation = 0.0;

    // Initial viewing angle.
    double view_angle = -0.1;

    bool running = true;

    while (running) {
        /*
            Start timing the frame so that the program can sleep for
            the remaining time needed for approximately 60 FPS.
        */
        long long frame_start = currentTimeMicros();

        // Handle keyboard input.
        running = handleUserInput(
            display,
            current_shape,
            show_axes,
            view_angle
        );

        if (!running) {
            break;
        }

        // Clear the framebuffer with a black background.
        for (unsigned int& pixel : framebuffer) {
            pixel = 0xff000000;
        }

        // Draw the coordinate axes if enabled.
        if (show_axes) {
            drawWorldAxes(
                framebuffer.data(),
                camera,
                view_angle
            );
        }

        // Copy the selected shape.
        shape_3d_t shape =
            shapes_list.at(current_shape);

        /*
            Update Y rotation.

            This is approximately 1.5 degrees per frame.
        */
        y_rotation += 0.026179916666666667;

        vector<point_3d_t> rotated_points =
            rotate_shape_around_y(
                shape.points,
                y_rotation
            );

        /*
            Update Z rotation.

            This is approximately 0.6 degrees per frame.
        */
        z_rotation += 0.010471966666666667;

        rotated_points =
            rotate_shape_around_z(
                rotated_points,
                z_rotation
            );

        shape.points = rotated_points;

        // Draw the rotated shape.
        drawShape(
            shape,
            framebuffer.data(),
            camera,
            view_angle
        );

        // Copy the framebuffer to the X11 window.
        XPutImage(
            display,
            window,
            gc,
            image,
            0,
            0,
            0,
            0,
            WIDTH,
            HEIGHT
        );

        XFlush(display);

        /*
            The target frame is approximately 16.67 ms.

            If drawing took less time than that, sleep for the
            remaining time. This keeps the animation near 60 FPS.
        */
        long long elapsed =
            currentTimeMicros() - frame_start;

        if (elapsed < FRAME_TIME_US) {
            usleep(
                static_cast<useconds_t>(
                    FRAME_TIME_US - elapsed
                )
            );
        }
    }

    /*
        XImage normally frees its data when destroyed. However, the
        image is using the vector's memory, so we set data to null
        before destroying the XImage to avoid freeing the vector's
        memory twice.
    */
    image->data = nullptr;

    XDestroyImage(image);

    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    cout << "Software Renderer Exiting!" << endl;

    return 0;
}