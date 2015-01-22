/* Aseprite
 * Copyright (C) 2001-2015  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/cmd/add_cel.h"
#include "app/cmd/replace_image.h"
#include "app/cmd/set_cel_position.h"
#include "app/cmd/unlink_cel.h"
#include "app/commands/command.h"
#include "app/context_access.h"
#include "app/document.h"
#include "app/document_api.h"
#include "app/modules/gui.h"
#include "app/transaction.h"
#include "base/unique_ptr.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "render/render.h"
#include "ui/ui.h"

namespace app {

class MergeDownLayerCommand : public Command {
public:
  MergeDownLayerCommand();
  Command* clone() const override { return new MergeDownLayerCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);
};

MergeDownLayerCommand::MergeDownLayerCommand()
  : Command("MergeDownLayer",
            "Merge Down Layer",
            CmdRecordableFlag)
{
}

bool MergeDownLayerCommand::onEnabled(Context* context)
{
  ContextWriter writer(context);
  Sprite* sprite(writer.sprite());
  if (!sprite)
    return false;

  Layer* src_layer = writer.layer();
  if (!src_layer || !src_layer->isImage())
    return false;

  Layer* dst_layer = src_layer->getPrevious();
  if (!dst_layer || !dst_layer->isImage())
    return false;

  return true;
}

void MergeDownLayerCommand::onExecute(Context* context)
{
  ContextWriter writer(context);
  Document* document(writer.document());
  Sprite* sprite(writer.sprite());
  Transaction transaction(writer.context(), "Merge Down Layer", ModifyDocument);
  Layer* src_layer = writer.layer();
  Layer* dst_layer = src_layer->getPrevious();

  for (frame_t frpos = 0; frpos<sprite->totalFrames(); ++frpos) {
    // Get frames
    Cel* src_cel = src_layer->cel(frpos);
    Cel* dst_cel = dst_layer->cel(frpos);

    // Get images
    Image* src_image;
    if (src_cel != NULL)
      src_image = src_cel->image();
    else
      src_image = NULL;

    ImageRef dst_image;
    if (dst_cel)
      dst_image = dst_cel->imageRef();

    // With source image?
    if (src_image) {
      // No destination image
      if (dst_image == NULL) {  // Only a transparent layer can have a null cel
        // Copy this cel to the destination layer...

        // Creating a copy of the image
        dst_image.reset(Image::createCopy(src_image));

        // Creating a copy of the cell
        dst_cel = new Cel(frpos, dst_image);
        dst_cel->setPosition(src_cel->x(), src_cel->y());
        dst_cel->setOpacity(src_cel->opacity());

        transaction.execute(new cmd::AddCel(dst_layer, dst_cel));
      }
      // With destination
      else {
        gfx::Rect bounds;

        // Merge down in the background layer
        if (dst_layer->isBackground()) {
          bounds = sprite->bounds();
        }
        // Merge down in a transparent layer
        else {
          bounds = src_cel->bounds().createUnion(dst_cel->bounds());
        }

        doc::color_t bgcolor = app_get_color_to_clear_layer(dst_layer);

        ImageRef new_image(doc::crop_image(dst_image,
            bounds.x-dst_cel->x(),
            bounds.y-dst_cel->y(),
            bounds.w, bounds.h, bgcolor));

        // Merge src_image in new_image
        render::composite_image(new_image, src_image,
          src_cel->x()-bounds.x,
          src_cel->y()-bounds.y,
          src_cel->opacity(),
          static_cast<LayerImage*>(src_layer)->getBlendMode());

        transaction.execute(new cmd::SetCelPosition(dst_cel,
            bounds.x, bounds.y));

        if (dst_cel->links())
          transaction.execute(new cmd::UnlinkCel(dst_cel));

        transaction.execute(new cmd::ReplaceImage(sprite,
            dst_cel->imageRef(), new_image));
      }
    }
  }

  document->notifyLayerMergedDown(src_layer, dst_layer);
  document->getApi(transaction).removeLayer(src_layer); // src_layer is deleted inside removeLayer()

  transaction.commit();
  update_screen_for_document(document);
}

Command* CommandFactory::createMergeDownLayerCommand()
{
  return new MergeDownLayerCommand;
}

} // namespace app
