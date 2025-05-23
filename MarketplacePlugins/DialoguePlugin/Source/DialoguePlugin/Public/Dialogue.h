// Copyright Underflow Studios 2017
#pragma once
#include "Runtime/Engine/Classes/Engine/DataAsset.h"
#include "CoreMinimal.h"
#include "Dialogue.generated.h"

class UDialogue;
// Extend this class to add custom events
UCLASS(Blueprintable, BlueprintType, abstract, EditInlineNew, HideCategories = ("DoNotShow"), CollapseCategories, AutoExpandCategories = ("Default"))
class DIALOGUEPLUGIN_API UDialogueEvents : public UObject
{
	GENERATED_BODY()
	
public:
	UDialogueEvents();

	//Called when the event is triggered. 
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Dialogue Events")
	void RecieveEventTriggered(APlayerController* ConsideringPlayer, AActor* NPCActor);

	virtual class UWorld* GetWorld() const override;
};

// Extend this class to add custom conditions.
UCLASS(Blueprintable, BlueprintType, abstract, EditInlineNew, HideCategories = ("DoNotShow"), CollapseCategories, AutoExpandCategories = ("Default"))
class DIALOGUEPLUGIN_API UDialogueConditions : public UObject
{
	GENERATED_BODY()
	
public:
	UDialogueConditions();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Dialogue Conditions")
	bool IsConditionMet(APlayerController* ConsideringPlayer, AActor* NPCActor);

	UFUNCTION()
	virtual bool IsConditionMet_Internal(APlayerController* ConsideringPlayer, AActor* NPCActor) { return true; }

	virtual class UWorld* GetWorld() const override;
};

// A particular type of condition that nests normal conditions and returns true if at least one of them is true.
UCLASS(NotBlueprintable, BlueprintType, EditInlineNew, HideCategories = ("DoNotShow"), CollapseCategories, AutoExpandCategories = ("Default"))
class DIALOGUEPLUGIN_API UOrCondition : public UDialogueConditions
{
	GENERATED_BODY()

public:
	UOrCondition(){ }

	UPROPERTY(Instanced, EditDefaultsOnly, Category = "Dialogue Conditions")
	TArray<UDialogueConditions*> OrConditions;

	virtual bool IsConditionMet_Internal(APlayerController* ConsideringPlayer, AActor* NPCActor) override
	{
		for (auto & cond : OrConditions)
		{
			if (cond->IsConditionMet(ConsideringPlayer, NPCActor))
				return true;
		}
		return false;
	}
};

// A particular type of condition that nests normal conditions and returns true if at least one of them is true.
UCLASS(NotBlueprintable, BlueprintType, EditInlineNew, HideCategories = ("DoNotShow"), CollapseCategories, AutoExpandCategories = ("Default"))
class DIALOGUEPLUGIN_API UAndCondition : public UDialogueConditions
{
	GENERATED_BODY()

public:
	UAndCondition(){ }

	UPROPERTY(Instanced, EditDefaultsOnly, Category = "Dialogue Conditions")
	TArray<UDialogueConditions*> AndConditions;

	virtual bool IsConditionMet_Internal(APlayerController* ConsideringPlayer, AActor* NPCActor) override
	{
		for (auto & cond : AndConditions)
		{
			if (!cond->IsConditionMet(ConsideringPlayer, NPCActor))
				return false;
		}
		return true;
	}
};


USTRUCT(BlueprintType)
struct FDialogueNode
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	int32 id = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	bool isPlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	TArray<int32> Links;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	FVector2D Coordinates  = FVector2D::ZeroVector;

	UPROPERTY(Instanced, EditDefaultsOnly, BlueprintReadOnly, Category = "Dialogue Node")
	TArray<UDialogueEvents*> Events;

	UPROPERTY(Instanced, EditDefaultsOnly, BlueprintReadOnly, Category = "Dialogue Node")
	TArray<UDialogueConditions*> Conditions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	class USoundBase* Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	class UDialogueWave* DialogueWave = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node", meta = (MultiLine = "true"))
	FText BubbleComment;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Node")
	bool bDrawBubbleComment = false;
};

class UGameInstance;

UCLASS(Blueprintable, BlueprintType)
class DIALOGUEPLUGIN_API UDialogue : public UDataAsset
{
	GENERATED_BODY()

public:

	UDialogue(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/* Gets the first user-created node. If the dialogue only has the orange node, returns an empty node with -1 as Id */
	UFUNCTION(BlueprintCallable, Category = Dialogue)
	FDialogueNode GetFirstNode();

	UFUNCTION(BlueprintCallable, Category = Dialogue)
	TArray<FDialogueNode> GetNextNodes(FDialogueNode Node);

	UFUNCTION(BlueprintCallable, Category = Dialogue)
	static void CallFunctionByName(UObject* Object, FString FunctionName);
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Dialogue)
	bool DisplayIdleSplines = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Dialogue)
	FText Name;

	// Not visible in the details panel unless ChatGPT integration is turned on
	/** NPC description, useful for providing context, like an NPC's profession or social standing.
	 * If no description is filled, it's not part of the prompt, saving on tokens. */
	UPROPERTY(BlueprintReadOnly, Category = Dialogue, meta = (MultiLine = "true"))
	FText Description;

	// Not visible in the details panel unless ChatGPT integration is turned on
	/** List of possible dispositions of the NPC towards the player. If "Undefined" is selected, the disposition is not part of the prompt, saving on tokens.
	 * Feel free to modify the other options. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chat GPT", meta = (GetOptions = "DialoguePluginEditor.DialoguePluginEditorSettings.GetDispositions"))
	FString NpcDisposition = TEXT("Undefined");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Dialogue)
	TArray<FDialogueNode> Data;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dialogue)
	int32 NextNodeId;

	// changing the hard pointer to weakptr seems to have fixed the crash
	TWeakObjectPtr<UGameInstance> PersistentGameInstance;

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void AssignPersistentOuter(class UGameInstance * inGameInstance);

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void CleanOuter();

	virtual UWorld* GetWorld() const override;	

	FDialogueNode GetNodeById(int32 id, int32 &index);
	FDialogueNode GetNodeById(int32 id);
	UFUNCTION(BlueprintCallable, Category = Dialogue)
	FDialogueNode GetNodeById(int32 id, bool& found);

	TArray<int32> SelectedNodes;
	int32 CurrentNodeId = -1; //-1 means no node is selected
	bool isLinking;
	FVector2D LinkingCoords;
	int32 LinkingFromIndex;

	bool bGenerating = false;
	bool bRefreshDetails = false;
};
