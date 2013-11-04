/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#ifndef MIR_GRAPHICS_ANDROID_FB_DEVICE_H_
#define MIR_GRAPHICS_ANDROID_FB_DEVICE_H_

#include "display_device.h"
#include <hardware/gralloc.h>
#include <hardware/fb.h>
 
namespace mir
{
namespace graphics
{
namespace android
{
class FramebufferBundle;

class FBDevice : public DisplayDevice 
{
public:
    FBDevice(std::shared_ptr<framebuffer_device_t> const& fbdev,
             std::shared_ptr<FramebufferBundle> const& bundle);

    geometry::Size display_size() const; 
    geometry::PixelFormat display_format() const; 
    unsigned int number_of_framebuffers_available() const;

    std::shared_ptr<graphics::Buffer> buffer_for_render();
    void set_next_frontbuffer(std::shared_ptr<graphics::Buffer> const& buffer);
    void sync_to_display(bool sync); 
    void mode(MirPowerMode mode);

    void commit_frame(EGLDisplay dpy, EGLSurface sur);

private:
    std::shared_ptr<framebuffer_device_t> const fb_device;
    std::shared_ptr<FramebufferBundle> const fb_bundle;

};

}
}
}
#endif /* MIR_GRAPHICS_ANDROID_FB_DEVICE_H_ */
