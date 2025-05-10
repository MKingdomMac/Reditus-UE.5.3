// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SPixel2DTileMapFlipbookBrowser.h"
#include "Pixel2DEdModeTileMap.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "FileHelpers.h"

#include "AssetRegistry/ARFilter.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SToolTip.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "FrontendFilterBase.h"
#include "ObjectEditorUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"

#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "Paper2DModule.h"

#define LOCTEXT_NAMESPACE "Pixel2DAnimAssetBrowser"


/** A filter that shows specific folders */
class FFrontendFilter_Folder : public FFrontendFilter
{
public:
	FFrontendFilter_Folder(TSharedPtr<FFrontendFilterCategory> InCategory, int32 InFolderIndex, FSimpleDelegate InOnActiveStateChanged)
		: FFrontendFilter(InCategory)
		, FolderIndex(InFolderIndex)
		, OnActiveStateChanged(InOnActiveStateChanged)
	{}

	// FFrontendFilter implementation
	virtual FString GetName() const override
	{
		return FString::Printf(TEXT("ShowFolder%d"), FolderIndex);
	}

	virtual FText GetDisplayName() const override
	{
		return Folder.IsEmpty() ? FText::Format(LOCTEXT("FolderFormatInvalid", "Show Specified Folder {0}"), FText::AsNumber(FolderIndex + 1)) : FText::Format(LOCTEXT("FolderFormatValid", "Folder: {0}"), FText::FromString(Folder));
	}

	virtual FText GetToolTipText() const override
	{
		return Folder.IsEmpty() ? LOCTEXT("FFrontendFilter_FolderToolTip", "Show assets in a specified folder") : FText::Format(LOCTEXT("FolderFormatValidToolTip", "Show assets in folder: {0}"), FText::FromString(Folder));
	}

	virtual FLinearColor GetColor() const override
	{
		return FLinearColor(0.6f, 0.6f, 0.0f, 1);
	}

	virtual void ModifyContextMenu(FMenuBuilder& MenuBuilder) override
	{
		MenuBuilder.BeginSection(TEXT("FolderSection"), LOCTEXT("FolderSectionHeading", "Choose Folder"));

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		FPathPickerConfig PathPickerConfig;
		PathPickerConfig.DefaultPath = Folder;
		PathPickerConfig.bAllowContextMenu = false;
		PathPickerConfig.OnPathSelected = FOnPathSelected::CreateLambda([this](const FString& InPath)
			{
				Folder = InPath;
				FSlateApplication::Get().DismissAllMenus();
				OnActiveStateChanged.ExecuteIfBound();
			});

		TSharedRef<SWidget> FolderWidget =
			SNew(SBox)
			.HeightOverride(300.0f)
			.WidthOverride(200.0f)
			[
				ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
			];

		MenuBuilder.AddWidget(FolderWidget, FText(), true);

		MenuBuilder.EndSection();
	}

	virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override
	{
		GConfig->SetString(*IniSection, *(SettingsString + TEXT(".Folder")), *Folder, IniFilename);
	}

	virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override
	{
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".Folder")), Folder, IniFilename);
	}

	virtual bool PassesFilter(FAssetFilterType InItem) const override
	{
		// Always pass this as a frontend filter, it acts as a backend filter
		return true;
	}

	virtual void ActiveStateChanged(bool bEnable)
	{
		bEnabled = bEnable;
		OnActiveStateChanged.ExecuteIfBound();
	}

public:
	/** Folder string to use when filtering */
	FString Folder;

	/** The index of this filter, for uniquely identifying this filter */
	int32 FolderIndex;

	/** Delegate fired to refresh the filter */
	FSimpleDelegate OnActiveStateChanged;

	/** Whether this filter is currently enabled */
	bool bEnabled;
};

SPixel2DTileMapFlipbookBrowser::~SPixel2DTileMapFlipbookBrowser()
{
}

void SPixel2DTileMapFlipbookBrowser::Construct(const FArguments& InArgs, TSharedPtr<FPixel2DEdModeTileMap> InTileMapEditor)
{
	TileMapEditorPtr = InTileMapEditor;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Configure filter for asset picker
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());

	FAssetPickerConfig Config;
	Config.Filter = Filter;
	Config.bAddFilterUI = true;
	Config.bShowPathInColumnView = true;
	Config.bSortByPathInColumnView = true;

	//// Configure response to click and double-click
	Config.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SPixel2DTileMapFlipbookBrowser::OnFlipbookSelected);
	//Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateSP(this, &SPixel2DAnimAssetBrowser::OnRequestOpenAsset, false);
	//Config.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SPixel2DAnimAssetBrowser::OnGetAssetContextMenu);
	Config.SyncToAssetsDelegates.Add(&SyncToAssetsDelegate);
	Config.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	Config.SetFilterDelegates.Add(&SetFilterDelegate);
	Config.bFocusSearchBoxWhenOpened = false;
	Config.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;

	//Config.SaveSettingsName = SettingsIniSection;

	TSharedPtr<FFrontendFilterCategory> FolderCategory = MakeShareable(new FFrontendFilterCategory(LOCTEXT("FolderFilters", "Folder Filters"), LOCTEXT("FolderFiltersTooltip", "Filter by folders.")));
	const uint32 NumFilters = 1;
	for (uint32 FilterIndex = 0; FilterIndex < NumFilters; ++FilterIndex)
	{
		TSharedRef<FFrontendFilter_Folder> FolderFilter = MakeShared<FFrontendFilter_Folder>(FolderCategory, FilterIndex,
			FSimpleDelegate::CreateLambda([this]()
				{
					Filter.PackagePaths.Empty();

					for (TSharedPtr<FFrontendFilter_Folder> CurrentFolderFilter : FolderFilters)
					{
						if (CurrentFolderFilter->bEnabled)
						{
							Filter.PackagePaths.Add(*CurrentFolderFilter->Folder);
						}
					}

					SetFilterDelegate.ExecuteIfBound(Filter);
				}));
		FolderFilters.Add(FolderFilter);
		Config.ExtraFrontendFilters.Add(FolderFilter);
	}

	//Config.OnIsAssetValidForCustomToolTip = FOnIsAssetValidForCustomToolTip::CreateLambda([](const FAssetData& AssetData) {return AssetData.IsAssetLoaded(); });
	//Config.OnGetCustomAssetToolTip = FOnGetCustomAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::CreateCustomAssetToolTip);
	//Config.OnVisualizeAssetToolTip = FOnVisualizeAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::OnVisualizeAssetToolTip);
	//Config.OnAssetToolTipClosing = FOnAssetToolTipClosing::CreateSP(this, &SPixel2DAnimAssetBrowser::OnAssetToolTipClosing);


	this->ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SBorder)
				.Padding(FMargin(3))
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					ContentBrowserModule.Get().CreateAssetPicker(Config)
				]
			]
		];
}

void SPixel2DTileMapFlipbookBrowser::OnFlipbookSelected(const FAssetData& AssetData)
{
	if (TileMapEditorPtr.IsValid())
	{
		UClass* AssetClass = FindObject<UClass>(AssetData.AssetClassPath);
		if (AssetClass->IsChildOf(UPaperFlipbook::StaticClass()) && AssetData.GetAsset())
		{
			UPaperFlipbook* Asset = StaticCast<UPaperFlipbook*>(AssetData.GetAsset());
			TileMapEditorPtr.Pin()->SetActiveFlipbook(Asset);
		}
	}
}

#undef LOCTEXT_NAMESPACE