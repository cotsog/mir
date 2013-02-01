/*
 * Copyright © 2012 Canonical Ltd.
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
 * Authored by: Thomas Voss <thomas.voss@canonical.com>
 */

#ifndef MIR_COMPOSITOR_DOUBLE_BUFFER_ALLOCATION_STRATEGY_H_
#define MIR_COMPOSITOR_DOUBLE_BUFFER_ALLOCATION_STRATEGY_H_

#include "mir/compositor/buffer_allocation_strategy.h"

namespace mir
{
namespace compositor
{

class GraphicBufferAllocator;
class BufferProperties;

class DoubleBufferAllocationStrategy : public BufferAllocationStrategy
{
public:
    explicit DoubleBufferAllocationStrategy(
        std::shared_ptr<GraphicBufferAllocator> const& gr_alloc);
    explicit DoubleBufferAllocationStrategy(
        std::shared_ptr<GraphicBufferAllocator> const& gr_alloc, int default_number_of_buffers);

    std::unique_ptr<BufferSwapper> create_swapper(
        BufferProperties& actual_buffer_properties,
        BufferProperties const& requested_buffer_properties);

private:
    std::shared_ptr<GraphicBufferAllocator> const gr_allocator;
    const int default_number_of_buffers;
};
}
}

#endif // MIR_COMPOSITOR_DOUBLE_BUFFER_ALLOCATION_STRATEGY_H_
