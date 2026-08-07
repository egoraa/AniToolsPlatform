// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_NODE_POSITION_HPP
#define ATP_STUDIO_NODE_POSITION_HPP

namespace atp::studio {

/// Position of a node on the canvas: editor metadata that never reaches the pipeline config and
/// lives in a sidecar file instead. It has a header of its own because the clipboard needs it and
/// the project needs the clipboard — leaving it in project.hpp would close that circle.
struct node_position {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const node_position&, const node_position&) = default;
};

}  // namespace atp::studio

#endif
