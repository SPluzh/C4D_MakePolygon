#include "makepolygon_command.h"
#include "polygon_builder.h"
#include "c4d_symbols.h"

namespace cinema
{

Int32 MakePolygonCommand::GetState(BaseDocument* doc, GeDialog* parentManager)
{
    if (!doc)
        return 0;

    BaseObject* op = doc->GetActiveObject();
    if (!op || !op->IsInstanceOf(Opolygon))
        return 0;

    return CMD_ENABLED;
}

Bool MakePolygonCommand::Execute(BaseDocument* doc, GeDialog* parentManager)
{
    if (!doc)
        return false;

    BaseObject* op = doc->GetActiveObject();
    if (!op || !op->IsInstanceOf(Opolygon))
    {
        GePrint("Make Polygon: Please select a Polygon Object."_s);
        StatusSetText("Make Polygon: Please select a Polygon Object."_s);
        return false;
    }

    PolygonObject* polyOp = static_cast<PolygonObject*>(op);

    // Prepare Undo
    doc->StartUndo();
    doc->AddUndo(UNDOTYPE::CHANGE, polyOp);

    MakePolygonResult result = PolygonBuilder::Execute(doc, polyOp);

    if (result.success)
    {
        polyOp->Message(MSG_UPDATE);
        doc->EndUndo();
        EventAdd();

        GePrint(result.message);
        StatusSetText(result.message);
        return true;
    }
    else
    {
        // Cancel undo since no changes were made
        doc->EndUndo();
        doc->DoUndo(true);

        if (result.message.IsPopulated())
        {
            GePrint(result.message);
            StatusSetText(result.message);
        }
        return false;
    }
}

Bool RegisterMakePolygonCommand()
{
    String name = GeLoadString(IDS_MAKEPOLYGON_COMMAND);
    if (name.IsEmpty() || name == "StrNotFound"_s)
        name = "Make Polygon (Modo P)"_s;

    String help = GeLoadString(IDS_MAKEPOLYGON_HELP);
    if (help.IsEmpty() || help == "StrNotFound"_s)
        help = "Creates polygon from points/edges or closes hole (Modo P)"_s;

    return RegisterCommandPlugin(
        PLUGIN_ID_MAKEPOLYGON,
        name,
        0,
        AutoBitmap("makepolygon.png"_s),
        help,
        NewObjClear(MakePolygonCommand)
    );
}

} // namespace cinema
