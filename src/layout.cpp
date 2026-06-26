/*
 *  Astick, the wayland compositor for ArcDE.
 *  Copyright (C) 2026 Eyad Ahmed Ragheb

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "layout.h"
#include "toplevel.h"
#include <cmath>

TilingLayout::TilingLayout()
    : QObject(nullptr)
{}

void TilingLayout::arrange(struct wlr_output *output, const QList<Toplevel *> &toplevels)
{
    if (output == nullptr || toplevels.isEmpty()) return;

    int width = output->width;
    int height = output->height;
    int count = toplevels.size();

    int cols = std::ceil(std::sqrt(count));
    int rows = std::ceil((double)count / cols);

    int cell_w = width / cols;
    int cell_h = height / rows;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = col * cell_w;
        int y = row * cell_h;
        wlr_scene_node_set_position(&toplevels[i]->getSceneTree()->node, x, y);
        wlr_xdg_toplevel_set_size(toplevels[i]->get(), cell_w, cell_h);
    }
}
