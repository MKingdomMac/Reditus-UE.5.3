// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SPixel2DAnimAssetBrowser.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Animation/AnimationAsset.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "FileHelpers.h"

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

const FString SPixel2DAnimAssetBrowser::SettingsIniSection = TEXT("Pixel2DAnimAssetBrowser");

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

////////////////////////////////////////////////////

const int32 SPixel2DAnimAssetBrowser::MaxAssetsHistory = 10;

SPixel2DAnimAssetBrowser::~SPixel2DAnimAssetBrowser()
{
	if (PreviewComponent)
	{
		for (int32 ComponentIdx = PreviewComponent->GetAttachChildren().Num() - 1; ComponentIdx >= 0; --ComponentIdx)
		{
			USceneComponent* Component = PreviewComponent->GetAttachChildren()[ComponentIdx];
			if (Component)
			{
				CleanupPreviewSceneComponent(Component);
			}
		}
		check(PreviewComponent->GetAttachChildren().Num() == 0);
	}

	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = NULL;
	}
}

void SPixel2DAnimAssetBrowser::CleanupPreviewSceneComponent(USceneComponent* Component)
{
	if (Component)
	{
		for (int32 ComponentIdx = Component->GetAttachChildren().Num() - 1; ComponentIdx >= 0; --ComponentIdx)
		{
			USceneComponent* ChildComponent = Component->GetAttachChildren()[ComponentIdx];
			CleanupPreviewSceneComponent(ChildComponent);
		}
		check(Component->GetAttachChildren().Num() == 0);
		Component->DestroyComponent();
	}
}

EActiveTimerReturnType SPixel2DAnimAssetBrowser::UpdateTootipPreview(double InCurrentTime, float InDeltaTime)
{
	bToolTipVisualizedThisFrame = false;
	if (PreviewComponent && IsToolTipPreviewVisible())
	{
		// Tick the world to update preview viewport for tooltips
		PreviewComponent->GetScene()->GetWorld()->Tick(LEVELTICK_All, InDeltaTime);
		// REVISIT - maybe just once at the beginning
		ViewportClient->FocusViewportOnBox(PreviewComponent->Bounds.GetBox(), true);
		ViewportClient->SetOrthoZoom(ViewportClient->GetOrthoZoom() * 4);
	}
	else
	{
		bIsActiveTimerRegistered = false;
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

void SPixel2DAnimAssetBrowser::OnRequestOpenAsset(const FAssetData& AssetData, bool bFromHistory)
{}

TSharedPtr<SWidget> SPixel2DAnimAssetBrowser::OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets)
{
	bool bHasSelectedAnimSequence = false;
	bool bHasSelectedAnimAsset = false;
	if (SelectedAssets.Num())
	{
		for (auto Iter = SelectedAssets.CreateConstIterator(); Iter; ++Iter)
		{
			UObject* Asset = Iter->GetAsset();
			// REVISIT
		}
	}

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/ true, Commands);

	MenuBuilder.BeginSection("AnimationSequenceOptions", LOCTEXT("OptionsHeading", "Options"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SaveSelectedAssets", "Save"),
			LOCTEXT("SaveSelectedAssets_ToolTip", "Save the selected assets"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Level.SaveIcon16x"),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPixel2DAnimAssetBrowser::SaveSelectedAssets, SelectedAssets),
				FCanExecuteAction::CreateSP(this, &SPixel2DAnimAssetBrowser::CanSaveSelectedAssets, SelectedAssets)
			)
		);

		MenuBuilder.AddMenuEntry(FGlobalEditorCommonCommands::Get().FindInContentBrowser);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SPixel2DAnimAssetBrowser::FindInContentBrowser()
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	if (CurrentSelection.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(CurrentSelection);
	}
}

bool SPixel2DAnimAssetBrowser::CanFindInContentBrowser() const
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	return CurrentSelection.Num() > 0;
}

void SPixel2DAnimAssetBrowser::GetSelectedPackages(const TArray<FAssetData>& Assets, TArray<UPackage*>& OutPackages) const
{
	for (int32 AssetIdx = 0; AssetIdx < Assets.Num(); ++AssetIdx)
	{
		UPackage* Package = FindPackage(NULL, *Assets[AssetIdx].PackageName.ToString());

		if (Package)
		{
			OutPackages.Add(Package);
		}
	}
}

void SPixel2DAnimAssetBrowser::SaveSelectedAssets(TArray<FAssetData> ObjectsToSave) const
{
	TArray<UPackage*> PackagesToSave;
	GetSelectedPackages(ObjectsToSave, PackagesToSave);

	const bool bCheckDirty = false;
	const bool bPromptToSave = false;
	const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}

bool SPixel2DAnimAssetBrowser::CanSaveSelectedAssets(TArray<FAssetData> ObjectsToSave) const
{
	TArray<UPackage*> Packages;
	GetSelectedPackages(ObjectsToSave, Packages);
	// Don't offer save option if none of the packages are loaded
	return Packages.Num() > 0;
}

bool SPixel2DAnimAssetBrowser::CanShowColumnForAssetRegistryTag(FName AssetType, FName TagName) const
{
	return !AssetRegistryTagsToIgnore.Contains(TagName);
}

void SPixel2DAnimAssetBrowser::Construct(const FArguments& InArgs)
{
	OnOpenNewAsset = InArgs._OnOpenNewAsset;
	bShowHistory = InArgs._ShowHistory;

	Commands = MakeShareable(new FUICommandList());
	Commands->MapAction(FGlobalEditorCommonCommands::Get().FindInContentBrowser, FUIAction(
		FExecuteAction::CreateSP(this, &SPixel2DAnimAssetBrowser::FindInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SPixel2DAnimAssetBrowser::CanFindInContentBrowser)
	));

	CurrentAssetHistoryIndex = INDEX_NONE;
	bTriedToCacheOrginalAsset = false;

	bIsActiveTimerRegistered = false;
	bToolTipVisualizedThisFrame = false;
	bToolTipClosedThisFrame = false;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	CreateAssetTooltipResources();

	// Configure filter for asset picker
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UPaperFlipbook::StaticClass()->GetClassPathName());

	FAssetPickerConfig Config;
	Config.Filter = Filter;
	Config.InitialAssetViewType = EAssetViewType::Column;
	Config.bAddFilterUI = true;
	Config.bShowPathInColumnView = true;
	Config.bSortByPathInColumnView = true;

	// Configure response to click and double-click
	Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateSP(this, &SPixel2DAnimAssetBrowser::OnRequestOpenAsset, false);
	Config.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SPixel2DAnimAssetBrowser::OnGetAssetContextMenu);
	Config.OnAssetTagWantsToBeDisplayed = FOnShouldDisplayAssetTag::CreateSP(this, &SPixel2DAnimAssetBrowser::CanShowColumnForAssetRegistryTag);
	Config.SyncToAssetsDelegates.Add(&SyncToAssetsDelegate);
	Config.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SPixel2DAnimAssetBrowser::HandleFilterAsset);
	Config.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	Config.SetFilterDelegates.Add(&SetFilterDelegate);
	Config.bFocusSearchBoxWhenOpened = false;
	Config.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;

	Config.SaveSettingsName = SettingsIniSection;

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

	Config.OnIsAssetValidForCustomToolTip = FOnIsAssetValidForCustomToolTip::CreateLambda([](const FAssetData& AssetData) {return AssetData.IsAssetLoaded(); });
	Config.OnGetCustomAssetToolTip = FOnGetCustomAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::CreateCustomAssetToolTip);
	Config.OnVisualizeAssetToolTip = FOnVisualizeAssetToolTip::CreateSP(this, &SPixel2DAnimAssetBrowser::OnVisualizeAssetToolTip);
	Config.OnAssetToolTipClosing = FOnAssetToolTipClosing::CreateSP(this, &SPixel2DAnimAssetBrowser::OnAssetToolTipClosing);

	// hide all asset registry columns by default (we only really want the name and path)
	TArray<UObject::FAssetRegistryTag> AssetRegistryTags;
	UAnimSequence::StaticClass()->GetDefaultObject()->GetAssetRegistryTags(AssetRegistryTags);
	for (UObject::FAssetRegistryTag& AssetRegistryTag : AssetRegistryTags)
	{
		Config.HiddenColumnNames.Add(AssetRegistryTag.Name.ToString());
	}

	// Also hide the type column by default (but allow users to enable it, so don't use bShowTypeInColumnView)
	Config.HiddenColumnNames.Add(TEXT("Class"));

	static const FName DefaultForegroundName("DefaultForeground");

	TSharedRef< SMenuAnchor > BackMenuAnchorPtr = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(this, &SPixel2DAnimAssetBrowser::CreateHistoryMenu, true)
		[
			SNew(SButton)
			.OnClicked(this, &SPixel2DAnimAssetBrowser::OnGoBackInHistory)
			.ForegroundColor(FAppStyle::GetSlateColor(DefaultForegroundName))
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(1, 0))
			.IsEnabled(this, &SPixel2DAnimAssetBrowser::CanStepBackwardInHistory)
			.ToolTipText(LOCTEXT("Backward_Tooltip", "Step backward in the asset history. Right click to see full history."))
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "ContentBrowser.TopBar.Font")
				.Font(FAppStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FText::FromString(FString(TEXT("\xf060"))) /*fa-arrow-left*/)
			]
		];

	TSharedRef< SMenuAnchor > FwdMenuAnchorPtr = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(this, &SPixel2DAnimAssetBrowser::CreateHistoryMenu, false)
		[
			SNew(SButton)
			.OnClicked(this, &SPixel2DAnimAssetBrowser::OnGoForwardInHistory)
			.ForegroundColor(FAppStyle::GetSlateColor(DefaultForegroundName))
			.ButtonStyle(FAppStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(1, 0))
			.IsEnabled(this, &SPixel2DAnimAssetBrowser::CanStepForwardInHistory)
			.ToolTipText(LOCTEXT("Forward_Tooltip", "Step forward in the asset history. Right click to see full history."))
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "ContentBrowser.TopBar.Font")
				.Font(FAppStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FText::FromString(FString(TEXT("\xf061"))) /*fa-arrow-right*/)
			]
		];

	this->ChildSlot
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Visibility(this, &SPixel2DAnimAssetBrowser::GetHistoryVisibility)
				.Padding(FMargin(3))
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.OnMouseButtonDown(this, &SPixel2DAnimAssetBrowser::OnMouseDownHistory, TWeakPtr<SMenuAnchor>(BackMenuAnchorPtr))
							.BorderImage(FAppStyle::GetBrush("NoBorder"))
							[
								BackMenuAnchorPtr
							]
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.OnMouseButtonDown(this, &SPixel2DAnimAssetBrowser::OnMouseDownHistory, TWeakPtr<SMenuAnchor>(FwdMenuAnchorPtr))
							.BorderImage(FAppStyle::GetBrush("NoBorder"))
							[
								FwdMenuAnchorPtr
							]
						]
					]
				]
			]
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

FReply SPixel2DAnimAssetBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Commands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SPixel2DAnimAssetBrowser::AddAssetToHistory(const FAssetData& AssetData)
{
}

FReply SPixel2DAnimAssetBrowser::OnMouseDownHistory(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TWeakPtr< SMenuAnchor > InMenuAnchor)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		InMenuAnchor.Pin()->SetIsOpen(true);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<SWidget> SPixel2DAnimAssetBrowser::CreateHistoryMenu(bool bInBackHistory) const
{
	FMenuBuilder MenuBuilder(true, NULL);
	if (bInBackHistory)
	{
		int32 HistoryIdx = CurrentAssetHistoryIndex - 1;
		while (HistoryIdx >= 0)
		{
			const FAssetData& AssetData = AssetHistory[HistoryIdx];

			if (AssetData.IsValid())
			{
				const FText DisplayName = FText::FromName(AssetData.AssetName);
				const FText Tooltip = FText::FromString(AssetData.GetObjectPathString());

				//MenuBuilder.AddMenuEntry(DisplayName, Tooltip, FSlateIcon(),
				//	FUIAction(
				//		FExecuteAction::CreateRaw(this, &SPixel2DAnimAssetBrowser::GoToHistoryIndex, HistoryIdx)
				//	),
				//	NAME_None, EUserInterfaceActionType::Button);
			}

			--HistoryIdx;
		}
	}
	else
	{
		int32 HistoryIdx = CurrentAssetHistoryIndex + 1;
		while (HistoryIdx < AssetHistory.Num())
		{
			const FAssetData& AssetData = AssetHistory[HistoryIdx];

			if (AssetData.IsValid())
			{
				const FText DisplayName = FText::FromName(AssetData.AssetName);
				const FText Tooltip = FText::FromString(AssetData.GetObjectPathString());

				//MenuBuilder.AddMenuEntry(DisplayName, Tooltip, FSlateIcon(),
				//	FUIAction(
				//		FExecuteAction::CreateRaw(this, &SPixel2DAnimAssetBrowser::GoToHistoryIndex, HistoryIdx)
				//	),
				//	NAME_None, EUserInterfaceActionType::Button);
			}

			++HistoryIdx;
		}
	}

	return MenuBuilder.MakeWidget();
}

bool SPixel2DAnimAssetBrowser::CanStepBackwardInHistory() const
{
	int32 HistoryIdx = CurrentAssetHistoryIndex - 1;
	while (HistoryIdx >= 0)
	{
		if (AssetHistory[HistoryIdx].IsValid())
		{
			return true;
		}

		--HistoryIdx;
	}
	return false;
}

bool SPixel2DAnimAssetBrowser::CanStepForwardInHistory() const
{
	int32 HistoryIdx = CurrentAssetHistoryIndex + 1;
	while (HistoryIdx < AssetHistory.Num())
	{
		if (AssetHistory[HistoryIdx].IsValid())
		{
			return true;
		}

		++HistoryIdx;
	}
	return false;
}

FReply SPixel2DAnimAssetBrowser::OnGoForwardInHistory()
{
	while (CurrentAssetHistoryIndex < AssetHistory.Num() - 1)
	{
		++CurrentAssetHistoryIndex;

		if (AssetHistory[CurrentAssetHistoryIndex].IsValid())
		{
			GoToHistoryIndex(CurrentAssetHistoryIndex);
			break;
		}
	}
	return FReply::Handled();
}

FReply SPixel2DAnimAssetBrowser::OnGoBackInHistory()
{
	while (CurrentAssetHistoryIndex > 0)
	{
		--CurrentAssetHistoryIndex;

		if (AssetHistory[CurrentAssetHistoryIndex].IsValid())
		{
			GoToHistoryIndex(CurrentAssetHistoryIndex);
			break;
		}
	}
	return FReply::Handled();
}

void SPixel2DAnimAssetBrowser::GoToHistoryIndex(int32 InHistoryIdx)
{
	if (AssetHistory[InHistoryIdx].IsValid())
	{
		CurrentAssetHistoryIndex = InHistoryIdx;
		OnRequestOpenAsset(AssetHistory[InHistoryIdx], /**bFromHistory=*/true);
	}
}

void SPixel2DAnimAssetBrowser::SelectAsset(UAnimationAsset * AnimAsset)
{
	FAssetData AssetData(AnimAsset);

	if (AssetData.IsValid())
	{
		TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();

		if (!CurrentSelection.Contains(AssetData))
		{
			TArray<FAssetData> AssetsToSelect;
			AssetsToSelect.Add(AssetData);

			SyncToAssetsDelegate.Execute(AssetsToSelect);
		}
	}
}

void SPixel2DAnimAssetBrowser::AddToHistory(UAnimationAsset * AnimAsset)
{
	if (AnimAsset)
	{
		FAssetData AssetData(AnimAsset);
		AddAssetToHistory(AssetData);
	}
}

TSharedRef<SToolTip> SPixel2DAnimAssetBrowser::CreateCustomAssetToolTip(FAssetData& AssetData)
{
	// Make a list of tags to show
	TArray<UObject::FAssetRegistryTag> Tags;
	UClass* AssetClass = FindObject<UClass>(AssetData.AssetClassPath);
	check(AssetClass);
	AssetClass->GetDefaultObject()->GetAssetRegistryTags(Tags);

	TArray<FName> TagsToShow;
	for (UObject::FAssetRegistryTag& TagEntry : Tags)
	{
		if (TagEntry.Name != FName(TEXT("Skeleton")) && TagEntry.Type != UObject::FAssetRegistryTag::TT_Hidden)
		{
			TagsToShow.Add(TagEntry.Name);
		}
	}

	// Add asset registry tags to a text list; except skeleton as that is implied in Persona
	TSharedRef<SVerticalBox> DescriptionBox = SNew(SVerticalBox);
	for (TPair<FName, FAssetTagValueRef> TagPair : AssetData.TagsAndValues)
	{
		if (TagsToShow.Contains(TagPair.Key))
		{
			// Check for DisplayName metadata
			FText DisplayName;
			if (FProperty* Field = FindFProperty<FProperty>(AssetClass, TagPair.Key))
			{
				DisplayName = Field->GetDisplayNameText();
			}
			else
			{
				DisplayName = FText::FromName(TagPair.Key);
			}

			DescriptionBox->AddSlot()
				.AutoHeight()
				.Padding(0, 0, 5, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("AssetTagKey", "{0}: "), DisplayName))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(TagPair.Value.AsText())
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				];
		}
	}

	DescriptionBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 5, 0)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AssetBrowser_FolderPathLabel", "Folder :"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromName(AssetData.PackagePath))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.WrapTextAt(300.f)
			]
		];

	TSharedPtr<SHorizontalBox> ContentBox = nullptr;
	TSharedRef<SToolTip> ToolTipWidget = SNew(SToolTip)
		.TextMargin(1)
		.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ToolTipBorder"))
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.NonContentBorder"))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 4)
				[
					SNew(SBorder)
					.Padding(6)
					.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
					[
						SNew(SBox)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(FText::FromName(AssetData.AssetName))
							.Font(FAppStyle::GetFontStyle("ContentBrowser.TileViewTooltip.NameFont"))
						]
					]
				]

				+SVerticalBox::Slot()
				[
					SAssignNew(ContentBox, SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBorder)
						.Padding(6)
						.Visibility(AssetClass->IsChildOf<UPaperFlipbook>() ? EVisibility::Visible : EVisibility::Collapsed)
						.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
						[
							SNew(SOverlay)
							+SOverlay::Slot()
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("NoPreviewFlipbook", "No Preview Flipbook"))
							]

							+ SOverlay::Slot()
							.VAlign(VAlign_Center)
							.HAlign(HAlign_Center)
							[
								ViewportWidget.ToSharedRef()
							]

						]
					]
				]
			]
		];

	// add an extra section to the tooltip for it.
	ContentBox->AddSlot()
		.Padding(AssetClass->IsChildOf<UPaperFlipbook>() ? 4 : 0, 0, 0, 0)
		[
			SNew(SBorder)
			.Padding(6)
			.BorderImage(FAppStyle::GetBrush("ContentBrowser.TileViewTooltip.ContentBorder"))
			[
				DescriptionBox
			]
		];

	return ToolTipWidget;
}


void SPixel2DAnimAssetBrowser::CreateAssetTooltipResources()
{
	SAssignNew(ViewportWidget, SViewport)
		.EnableGammaCorrection(false)
		.ViewportSize(FVector2D(128, 128));

	ViewportClient = MakeShareable(new FPixel2DAnimAssetViewportClient(PreviewScene));
	SceneViewport = MakeShareable(new FSceneViewport(ViewportClient.Get(), ViewportWidget));
	PreviewComponent = NewObject<UPaperFlipbookComponent>();

	// Client options
	ViewportClient->ViewportType = LVT_OrthoXZ;
	ViewportClient->bSetListenerPosition = true;
	SceneViewport->SetViewportSize(128, 128);

	ViewportClient->Viewport = SceneViewport.Get();
	ViewportClient->SetRealtime(true);
	ViewportClient->SetViewMode(VMI_Lit);
	ViewportClient->ToggleOrbitCamera(false);
	ViewportClient->VisibilityDelegate.BindSP(this, &SPixel2DAnimAssetBrowser::IsToolTipPreviewVisible);

	// Add the scene viewport
	ViewportWidget->SetViewportInterface(SceneViewport.ToSharedRef());

	// Setup the preview component to ensure an animation will update when requested
	PreviewScene.AddComponent(PreviewComponent, FTransform::Identity);
}

bool SPixel2DAnimAssetBrowser::OnVisualizeAssetToolTip(const TSharedPtr<SWidget>& TooltipContent, FAssetData& AssetData)
{
	// Resolve the asset
	UPaperFlipbookComponent* MeshToUse = nullptr;
	UClass* AssetClass = FindObject<UClass>(AssetData.AssetClassPath);
	if (AssetClass->IsChildOf(UPaperFlipbook::StaticClass()) /*&& AssetData.IsAssetLoaded() */ && AssetData.GetAsset())
	{
		// Set up the viewport to show the asset. Catching the visualize allows us to use
		// one viewport between all of the assets in the flipbook browser.
		UPaperFlipbook* Asset = StaticCast<UPaperFlipbook*>(AssetData.GetAsset());
		{
			PreviewComponent->SetFlipbook(Asset);
			PreviewComponent->UpdateBounds();

			ViewportClient->FocusViewportOnBox(PreviewComponent->Bounds.GetBox(), true);
			ViewportClient->SetOrthoZoom(ViewportClient->GetOrthoZoom() * 4); // REVISIT - why 4?

			ViewportWidget->SetVisibility(EVisibility::Visible);

			// Update the preview as long as the tooltip is visible
			if (!bIsActiveTimerRegistered)
			{
				bIsActiveTimerRegistered = true;
				RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SPixel2DAnimAssetBrowser::UpdateTootipPreview));
			}
			bToolTipVisualizedThisFrame = true;
		}
	}

	// We return false here as we aren't visualizing the tooltip - just detecting when it is about to be shown.
	// We still want slate to draw it.
	return false;
}

void SPixel2DAnimAssetBrowser::OnAssetToolTipClosing()
{
	// Make sure that the tooltip isn't about to preview another animation
	if (!bToolTipVisualizedThisFrame)
	{
		ViewportWidget->SetVisibility(EVisibility::Hidden);
	}
}

bool SPixel2DAnimAssetBrowser::IsToolTipPreviewVisible()
{
	bool bVisible = false;
	if (ViewportWidget.IsValid())
	{
		bVisible = ViewportWidget->GetVisibility() == EVisibility::Visible;
	}
	return bVisible;
}

EVisibility SPixel2DAnimAssetBrowser::GetHistoryVisibility() const
{
	return bShowHistory ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SPixel2DAnimAssetBrowser::HandleFilterAsset(const FAssetData& InAssetData) const
{
	return false;
}

FPixel2DAnimAssetViewportClient::FPixel2DAnimAssetViewportClient(FPreviewScene& InPreviewScene)
	: FEditorViewportClient(nullptr, &InPreviewScene)
{
	const bool XX = FMath::IsNearlyEqual(PaperAxisX.X, 1.0f);
	const bool XY = FMath::IsNearlyEqual(PaperAxisX.Y, 1.0f);
	const bool YY = FMath::IsNearlyEqual(PaperAxisY.Y, 1.0f);
	const bool YZ = FMath::IsNearlyEqual(PaperAxisY.Z, 1.0f);

	ELevelViewportType NewViewportType = LVT_OrthoXZ;
	if (XX && YY)
	{
		NewViewportType = LVT_OrthoXY;
	}
	else if (XX && YZ)
	{
		NewViewportType = LVT_OrthoXZ;
	}
	else if (XY && YZ)
	{
		NewViewportType = LVT_OrthoYZ;
	}
	else
	{
		//@TODO: Unsupported axes
	}

	SetViewportType(NewViewportType);
	SetViewModes(VMI_Lit, VMI_Lit);

	// Always composite editor objects after post processing in the editor
	EngineShowFlags.SetCompositeEditorPrimitives(true);
	EngineShowFlags.DisableAdvancedFeatures();

	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.GridColorAxis = FColor(70, 70, 70);
	DrawHelper.GridColorMajor = FColor(40, 40, 40);
	DrawHelper.GridColorMinor = FColor(20, 20, 20);
	DrawHelper.AxesLineThickness = 0;
	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;
	//bDrawAxes = false;
	bDrawAxes = false;

	if (IsSetShowGridChecked())
	{
		SetShowGrid();
	}
}

FSceneInterface* FPixel2DAnimAssetViewportClient::GetScene() const
{
	return PreviewScene->GetScene();
}

FLinearColor FPixel2DAnimAssetViewportClient::GetBackgroundColor() const
{
	return FLinearColor(0.1f, 0.1f, 0.1f);
	return FLinearColor(0.8f, 0.85f, 0.85f);
}

#undef LOCTEXT_NAMESPACE
