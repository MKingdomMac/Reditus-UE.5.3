#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/** Anim Graph Commands */
class PIXEL2DEDITOR_API FPixel2DAnimGraphCommands : public TCommands<FPixel2DAnimGraphCommands>
{
public:
	FPixel2DAnimGraphCommands()
		: TCommands<FPixel2DAnimGraphCommands>(TEXT("Pixel2DAnimGraph"), NSLOCTEXT("Contexts", "Pixel2DAnimGraphCommands", "Pixel2D Anim Graph Commands"), NAME_None, FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:

	// option for opening the asset related to the graph node
	TSharedPtr< FUICommandInfo > OpenRelatedAsset;
};
