/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */

#include "src/server/input/surface_input_dispatcher.h"

#include "mir/events/event_builders.h"
#include "mir/events/event_private.h"
#include "mir/scene/observer.h"
#include "mir/thread_safe_list.h"

#include "mir/test/event_matchers.h"
#include "mir/test/fake_shared.h"
#include "mir_test_doubles/stub_input_scene.h"
#include "mir_test_doubles/mock_surface.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <mutex>
#include <algorithm>

namespace ms = mir::scene;
namespace mi = mir::input;
namespace mev = mir::events;
namespace mt = mir::test;
namespace mtd = mt::doubles;

namespace geom = mir::geometry;

namespace
{

struct MockSurfaceWithGeometry : public mtd::MockSurface
{
    MockSurfaceWithGeometry(geom::Rectangle const& geom)
        : geom(geom)
    {
    }

    bool input_area_contains(geom::Point const& p) const override
    {
        return geom.contains(p);
    }

    geom::Rectangle input_bounds() const override
    {
        return geom;
    }
    
    geom::Rectangle const geom;
};

struct StubInputScene : public mtd::StubInputScene
{
    std::shared_ptr<mtd::MockSurface> add_surface(geom::Rectangle const& geometry)
    {
        auto surface = std::make_shared<MockSurfaceWithGeometry>(geometry);
        surfaces.add(surface);

        observer->surface_added(surface.get());
        
        return surface;
    }

    void remove_surface(std::shared_ptr<ms::Surface> const& surface)
    {
        surfaces.remove(surface);
        observer->surface_removed(surface.get());
    }

    std::shared_ptr<mtd::MockSurface> add_surface()
    {
        return add_surface({{0, 0}, {1, 1}});
    }
    
    void for_each(std::function<void(std::shared_ptr<mi::Surface> const&)> const& exec) override
    {
	surfaces.for_each([&exec](std::shared_ptr<mi::Surface> const& surface) {
            exec(surface);
        });
    }

    void add_observer(std::shared_ptr<ms::Observer> const& new_observer) override
    {
        assert(observer == nullptr);
        observer = new_observer;
	surfaces.for_each([this](std::shared_ptr<ms::Surface> const& surface) {
		observer->surface_exists(surface.get());
        });
    }
    
    void remove_observer(std::weak_ptr<ms::Observer> const& /* remove_observer */) override
    {
        assert(observer != nullptr);
        observer->end_observation();
        observer.reset();
    }
    
    mir::ThreadSafeList<std::shared_ptr<ms::Surface>> surfaces;

    std::shared_ptr<ms::Observer> observer;
};

struct SurfaceInputDispatcher : public testing::Test
{
    SurfaceInputDispatcher()
        : dispatcher(mt::fake_shared(scene))
    {
    }

    void TearDown() override { dispatcher.stop(); }

    StubInputScene scene;
    mi::SurfaceInputDispatcher dispatcher;
};

struct FakeKeyboard
{
    FakeKeyboard(MirInputDeviceId id = 0)
        : id(id)
    {
    }
    mir::EventUPtr press(int scan_code = 7)
    {
        return mev::make_event(id, std::chrono::nanoseconds(0),
	    mir_keyboard_action_down, 0, scan_code, mir_input_event_modifier_alt);
    }
    mir::EventUPtr release(int scan_code = 7)
    {
        return mev::make_event(id, std::chrono::nanoseconds(0),
	    mir_keyboard_action_up, 0, scan_code, mir_input_event_modifier_alt);
    }
    MirInputDeviceId const id;
};

struct FakePointer
{
    FakePointer(MirInputDeviceId id = 0)
        : id(id),
          buttons(0)
    {
    }

    mir::EventUPtr move_to(geom::Point const& location)
    {
        return mev::make_event(id, std::chrono::nanoseconds(0),
            0, mir_pointer_action_motion, buttons,
            location.x.as_int(), location.y.as_int(),
            0, 0);
    }
    mir::EventUPtr release_button(geom::Point const& location, MirPointerButton button = mir_pointer_button_primary)
    {
        buttons &= ~button;

        return mev::make_event(id, std::chrono::nanoseconds(0),
            0, mir_pointer_action_button_up, buttons,
            location.x.as_int(), location.y.as_int(),
            0, 0);
    }
    mir::EventUPtr press_button(geom::Point const& location, MirPointerButton button = mir_pointer_button_primary)
    {
        buttons |= button;
        
        return mev::make_event(id, std::chrono::nanoseconds(0),
            0, mir_pointer_action_button_down, buttons,
            location.x.as_int(), location.y.as_int(),
            0, 0);
    }

    MirInputDeviceId const id;
    MirPointerButtons buttons;
};

struct FakeToucher
{
    FakeToucher(MirInputDeviceId id = 0)
        : id(id)
    {
    }
    
    mir::EventUPtr move_to(geom::Point const& point)
    {
        auto ev = mev::make_event(id, std::chrono::nanoseconds(0), 0);
        mev::add_touch(*ev, 0, mir_touch_action_change,
                       mir_touch_tooltype_finger, point.x.as_int(), point.y.as_int(),
                       touched ? 1.0 : 0.0,
                       touched ? 1.0 : 0.0,
                       touched ? 1.0 : 0.0,
                       touched ? 1.0 : 0.0);
        return ev;
    }
    mir::EventUPtr touch_at(geom::Point const& point)
    {
        touched = true;
        
        auto ev = mev::make_event(id, std::chrono::nanoseconds(0), 0);
        mev::add_touch(*ev, 0, mir_touch_action_down,
                       mir_touch_tooltype_finger, point.x.as_int(), point.y.as_int(),
                       1.0, 1.0, 1.0, 1.0);
        return ev;
    }
    mir::EventUPtr release_at(geom::Point const& point)
    {
        touched = false;
        
        auto ev = mev::make_event(id, std::chrono::nanoseconds(0), 0);
        mev::add_touch(*ev, 0, mir_touch_action_up,
                       mir_touch_tooltype_finger, point.x.as_int(), point.y.as_int(),
                       0.0, 0.0, 0.0, 0.0);
        return ev;
    }
    bool touched = false;
    MirInputDeviceId const id;
};

}

TEST_F(SurfaceInputDispatcher, key_event_delivered_to_focused_surface)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface();

    FakeKeyboard keyboard;
    auto event = keyboard.press();

    EXPECT_CALL(*surface, consume(mt::MirKeyEventMatches(*event))).Times(1);

    dispatcher.start();

    dispatcher.set_focus(surface);
    EXPECT_TRUE(dispatcher.dispatch(*event));
}

TEST_F(SurfaceInputDispatcher, key_event_dropped_if_no_surface_focused)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface();
    
    EXPECT_CALL(*surface, consume(_)).Times(0);

    dispatcher.start();

    FakeKeyboard keyboard;
    EXPECT_FALSE(dispatcher.dispatch(*keyboard.press()));
}

TEST_F(SurfaceInputDispatcher, inconsistent_key_events_dropped)
{
    using namespace ::testing;

    auto surface = scene.add_surface();

    EXPECT_CALL(*surface, consume(_)).Times(0);

    dispatcher.start();

    dispatcher.set_focus(surface);

    FakeKeyboard keyboard;
    EXPECT_FALSE(dispatcher.dispatch(*keyboard.release()));
}

TEST_F(SurfaceInputDispatcher, key_state_is_consistent_per_client)
{
    using namespace ::testing;

    auto surface_1 = scene.add_surface();
    auto surface_2 = scene.add_surface();

    FakeKeyboard keyboard;
    auto down_event = keyboard.press();
    auto up_event = keyboard.release();

    EXPECT_CALL(*surface_1, consume(mt::MirKeyEventMatches(*down_event))).Times(1);
    EXPECT_CALL(*surface_2, consume(_)).Times(0);

    dispatcher.start();

    dispatcher.set_focus(surface_1);
    EXPECT_TRUE(dispatcher.dispatch(*down_event));
    dispatcher.set_focus(surface_2);
    EXPECT_FALSE(dispatcher.dispatch(*up_event));
}

TEST_F(SurfaceInputDispatcher, inconsistent_key_down_dropped)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface();

    FakeKeyboard keyboard;
    auto event = keyboard.press();

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::MirKeyEventMatches(*event))).Times(1);

    dispatcher.start();

    dispatcher.set_focus(surface);
    EXPECT_TRUE(dispatcher.dispatch(*event));
    EXPECT_FALSE(dispatcher.dispatch(*event));
    EXPECT_FALSE(dispatcher.dispatch(*event));
}

TEST_F(SurfaceInputDispatcher, device_reset_resets_key_state_consistency)
{
    using namespace ::testing;

    auto surface = scene.add_surface();

    auto device_id = MirInputDeviceId{1};
    FakeKeyboard keyboard(device_id);
    auto down_event = keyboard.press(11);
    auto release_event = keyboard.release(11);

    dispatcher.start();

    dispatcher.set_focus(surface);
    EXPECT_TRUE(dispatcher.dispatch(*down_event));
    EXPECT_TRUE(dispatcher.dispatch(
        *mev::make_event(mir_input_configuration_action_device_reset, device_id, std::chrono::nanoseconds{1})));
    EXPECT_FALSE(dispatcher.dispatch(*release_event));
}

TEST_F(SurfaceInputDispatcher, key_input_target_may_disappear_and_things_remain_quote_a_unquote_ok)
{
    using namespace ::testing;
    
    auto surface_2 = scene.add_surface();
    auto surface_1 = scene.add_surface();

    EXPECT_CALL(*surface_1, consume(_)).Times(AnyNumber());
    EXPECT_CALL(*surface_2, consume(_)).Times(AnyNumber());

    FakeKeyboard k;
    auto an_ev = k.press(11);
    auto another_ev = k.press(12);

    dispatcher.start();
    dispatcher.set_focus(surface_1);
    EXPECT_TRUE(dispatcher.dispatch(*an_ev));
    scene.remove_surface(surface_1);
    EXPECT_FALSE(dispatcher.dispatch(*another_ev));
    dispatcher.set_focus(surface_2);
    EXPECT_TRUE(dispatcher.dispatch(*another_ev));
}

TEST_F(SurfaceInputDispatcher, pointer_motion_delivered_to_client_under_pointer)
{
    using namespace ::testing;

    auto surface = scene.add_surface({{0, 0}, {5, 5}});

    FakePointer pointer;
    auto ev_1 = pointer.move_to({1, 0});
    auto ev_2 = pointer.move_to({5, 0});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEventWithPosition(1, 0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerLeaveEvent())).Times(1);

    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({1, 0})));
    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({5, 0})));
}

TEST_F(SurfaceInputDispatcher, pointer_delivered_only_to_top_surface)
{
    using namespace ::testing;

    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto top_surface = scene.add_surface({{0, 0}, {5, 5}});

    FakePointer pointer;

    InSequence seq;
    EXPECT_CALL(*top_surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*top_surface, consume(mt::PointerEventWithPosition(1, 0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEventWithPosition(1, 0))).Times(1);
    
    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({1, 0})));
    scene.remove_surface(top_surface);
    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({1, 0})));
}

TEST_F(SurfaceInputDispatcher, pointer_may_move_between_adjacent_surfaces)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto another_surface = scene.add_surface({{5, 5}, {5, 5}});

    FakePointer pointer;
    auto ev_1 = pointer.move_to({6, 6});
    auto ev_2 = pointer.move_to({7, 7});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEventWithPosition(1, 1))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerLeaveEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEventWithPosition(1, 1))).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerLeaveEvent())).Times(1);
    
    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({1, 1})));
    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({6, 6})));
    EXPECT_TRUE(dispatcher.dispatch(*pointer.move_to({11, 11})));
}

// We test that a client will receive pointer events following a button down
// until the pointer comes up.
TEST_F(SurfaceInputDispatcher, gestures_persist_over_button_down)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto another_surface = scene.add_surface({{5, 5}, {5, 5}});

    FakePointer pointer;
    auto ev_1 = pointer.press_button({0, 0});
    auto ev_2 = pointer.move_to({6, 6});
    auto ev_3 = pointer.release_button({6, 6});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonDownEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEventWithPosition(6, 6))).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonUpEvent(6,6))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerLeaveEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEnterEvent())).Times(1);
    
    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*ev_1));
    EXPECT_TRUE(dispatcher.dispatch(*ev_2));
    EXPECT_TRUE(dispatcher.dispatch(*ev_3));
}

TEST_F(SurfaceInputDispatcher, gestures_terminated_by_device_reset)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto another_surface = scene.add_surface({{5, 5}, {5, 5}});

    MirInputDeviceId device_id{1};
    FakePointer pointer(device_id);
    auto ev_1 = pointer.press_button({0, 0});
    auto ev_2 = pointer.move_to({6, 6});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonDownEvent(0,0))).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEventWithPosition(1, 1))).Times(1);
    
    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*ev_1));
    EXPECT_TRUE(dispatcher.dispatch(
        *mev::make_event(mir_input_configuration_action_device_reset, device_id, std::chrono::nanoseconds{1})));
    EXPECT_TRUE(dispatcher.dispatch(*ev_2));
}

TEST_F(SurfaceInputDispatcher, pointer_gestures_may_transfer_over_buttons)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto another_surface = scene.add_surface({{5, 5}, {5, 5}});

    FakePointer pointer;
    auto ev_1 = pointer.press_button({0, 0}, mir_pointer_button_primary);
    auto ev_2 = pointer.press_button({0, 0}, mir_pointer_button_secondary);
    auto ev_3 = pointer.release_button({0, 0}, mir_pointer_button_primary);
    auto ev_4 = pointer.move_to({6, 6});
    auto ev_5 = pointer.release_button({6, 6}, mir_pointer_button_secondary);

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonDownEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonDownEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonUpEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerEventWithPosition(6, 6))).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonUpEvent(6,6))).Times(1);
    EXPECT_CALL(*surface, consume(mt::PointerLeaveEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEnterEvent())).Times(1);

    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*ev_1));
    EXPECT_TRUE(dispatcher.dispatch(*ev_2));
    EXPECT_TRUE(dispatcher.dispatch(*ev_3));
    EXPECT_TRUE(dispatcher.dispatch(*ev_4));
    EXPECT_TRUE(dispatcher.dispatch(*ev_5));
}

TEST_F(SurfaceInputDispatcher, pointer_gesture_target_may_vanish_and_the_situation_remains_hunky_dorey)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{0, 0}, {5, 5}});
    auto another_surface = scene.add_surface({{5, 5}, {5, 5}});

    FakePointer pointer;
    auto ev_1 = pointer.press_button({0, 0});
    auto ev_2 = pointer.release_button({0, 0});
    auto ev_3 = pointer.move_to({6, 6});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*surface, consume(mt::ButtonDownEvent(0,0))).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEnterEvent())).Times(1);
    EXPECT_CALL(*another_surface, consume(mt::PointerEventWithPosition(1,1))).Times(1);
    
    dispatcher.start();

    EXPECT_TRUE(dispatcher.dispatch(*ev_1));
    scene.remove_surface(surface);
    EXPECT_FALSE(dispatcher.dispatch(*ev_2));
    EXPECT_TRUE(dispatcher.dispatch(*ev_3));
}

TEST_F(SurfaceInputDispatcher, touch_delivered_to_surface)
{
    using namespace ::testing;
    
    auto surface = scene.add_surface({{1, 1}, {1, 1}});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::TouchEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::TouchUpEvent(0,0))).Times(1);

    dispatcher.start();

    FakeToucher toucher;
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({1,1})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.release_at({1,1})));
}

TEST_F(SurfaceInputDispatcher, touch_delivered_only_to_top_surface)
{
    using namespace ::testing;
    
    auto bottom_surface = scene.add_surface({{1, 1}, {3, 3}});
    auto surface = scene.add_surface({{1, 1}, {3, 3}});

    InSequence seq;
    EXPECT_CALL(*surface, consume(mt::TouchEvent(0,0))).Times(1);
    EXPECT_CALL(*surface, consume(mt::TouchUpEvent(1,1))).Times(1);
    EXPECT_CALL(*bottom_surface, consume(mt::TouchEvent(0,0))).Times(0);
    EXPECT_CALL(*bottom_surface, consume(mt::TouchUpEvent(0,0))).Times(0);

    dispatcher.start();

    FakeToucher toucher;
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({1,1})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.release_at({2,2})));
}

TEST_F(SurfaceInputDispatcher, gestures_persist_over_touch_down)
{
    using namespace ::testing;
    
    auto left_surface = scene.add_surface({{0, 0}, {1, 1}});
    auto right_surface = scene.add_surface({{1, 1}, {1, 1}});

    InSequence seq;
    EXPECT_CALL(*left_surface, consume(mt::TouchEvent(0, 0))).Times(1);
    EXPECT_CALL(*left_surface, consume(mt::TouchMovementEvent())).Times(1);
    EXPECT_CALL(*left_surface, consume(mt::TouchUpEvent(2, 2))).Times(1);
    EXPECT_CALL(*right_surface, consume(_)).Times(0);

    dispatcher.start();
    
    FakeToucher toucher;
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({0, 0})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.move_to({2, 2})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.release_at({2, 2})));
}

TEST_F(SurfaceInputDispatcher, touch_gestures_terminated_by_device_reset)
{
    using namespace ::testing;
    
    auto left_surface = scene.add_surface({{0, 0}, {1, 1}});
    auto right_surface = scene.add_surface({{1, 1}, {1, 1}});

    MirInputDeviceId device_id{1};
    FakeToucher toucher(device_id);

    InSequence seq;
    EXPECT_CALL(*left_surface, consume(mt::TouchEvent(0, 0))).Times(1);
    EXPECT_CALL(*right_surface, consume(mt::TouchEvent(0, 0))).Times(1);

    dispatcher.start();
    
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({0, 0})));
    EXPECT_TRUE(dispatcher.dispatch(
        *mev::make_event(mir_input_configuration_action_device_reset, device_id, std::chrono::nanoseconds{1})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({1, 1})));
}

TEST_F(SurfaceInputDispatcher, touch_gesture_target_may_vanish_but_things_continue_to_function_as_intended)
{
    using namespace ::testing;
    
    auto surface_1 = scene.add_surface({{0, 0}, {1, 1}});
    auto surface_2 = scene.add_surface({{0, 0}, {1, 1}});

    InSequence seq;
    EXPECT_CALL(*surface_2, consume(mt::TouchEvent(0, 0))).Times(1);
    EXPECT_CALL(*surface_1, consume(mt::TouchEvent(0, 0))).Times(1);

    dispatcher.start();
    
    FakeToucher toucher;
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({0, 0})));
    scene.remove_surface(surface_2);
    EXPECT_FALSE(dispatcher.dispatch(*toucher.release_at({0, 0})));
    EXPECT_TRUE(dispatcher.dispatch(*toucher.touch_at({0, 0})));
}
