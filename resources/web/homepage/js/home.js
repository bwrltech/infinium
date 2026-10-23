//var TestData={"sequence_id":"0","command":"get_recent_projects","response":[{"path":"D:\\work\\Models\\Toy\\3d-puzzle-cube-model_files\\3d-puzzle-cube.3mf","time":"2022\/3\/24 20:33:10"},{"path":"D:\\work\\Models\\Art\\Carved Stone Vase - remeshed+drainage\\Carved Stone Vase.3mf","time":"2022\/3\/24 17:11:51"},{"path":"D:\\work\\Models\\Art\\Kity & Cat\\Cat.3mf","time":"2022\/3\/24 17:07:55"},{"path":"D:\\work\\Models\\Toy\\鐩村墤.3mf","time":"2022\/3\/24 17:06:02"},{"path":"D:\\work\\Models\\Toy\\minimalistic-dual-tone-whistle-model_files\\minimalistic-dual-tone-whistle.3mf","time":"2022\/3\/22 21:12:22"},{"path":"D:\\work\\Models\\Toy\\spiral-city-model_files\\spiral-city.3mf","time":"2022\/3\/22 18:58:37"},{"path":"D:\\work\\Models\\Toy\\impossible-dovetail-puzzle-box-model_files\\impossible-dovetail-puzzle-box.3mf","time":"2022\/3\/22 20:08:40"}]};

var m_HotModelList=null;

function OnInit()
{
	//-----Official-----
    TranslatePage();

	SendMsg_GetLoginInfo();
	SendMsg_GetBambuLoginInfo();
	SendMsg_GetRecentFile();
	SendMsg_GetStaffPick();

	Set_AccountMenu_Event();

	// INFINIUM: the start tiles work from the keyboard too
	$(document).on("keydown", ".BigTile, .FunItem, .GalleryItem, #GalleryBack, #GalleryMore", function(e) {
		if (e.key === "Enter" || e.key === " ") $(this).click();
	});
}

//------最佳打开文件的右键菜单功能----------
var RightBtnFilePath='';

var MousePosX=0;
var MousePosY=0;
var sImages = {};
 
function Set_RecentFile_MouseRightBtn_Event()
{
	$(".FileItem").mousedown(
		function(e)
		{			
			//FilePath
			RightBtnFilePath=$(this).attr('fpath');
			
			if(e.which == 3){
				//鼠标点击了右键+$(this).attr('ff') );
				ShowRecnetFileContextMenu();
			}else if(e.which == 2){
				//鼠标点击了中键
			}else if(e.which == 1){
				//鼠标点击了左键
				OnOpenRecentFile( encodeURI(RightBtnFilePath) );
			}
		});

	$(document).bind("contextmenu",function(e){
		//在这里书写代码，构建个性右键化菜单
		return false;
	});	
	
    $(document).mousemove( function(e){
		MousePosX=e.pageX;
		MousePosY=e.pageY;
		
		let ContextMenuWidth=$('#recnet_context_menu').width();
		let ContextMenuHeight=$('#recnet_context_menu').height();
	
		let DocumentWidth=$(document).width();
		let DocumentHeight=$(document).height();
		
		//$("#DebugText").text( ContextMenuWidth+' - '+ContextMenuHeight+'<br/>'+
		//					 DocumentWidth+' - '+DocumentHeight+'<br/>'+
		//					 MousePosX+' - '+MousePosY +'<br/>' );
	} );
	

	$(document).click( function(){		
		var e = e || window.event;
        var elem = e.target || e.srcElement;
        while (elem) {
			if (elem.id && elem.id == 'recnet_context_menu') {
                    return;
			}
			elem = elem.parentNode;
		}		
		
		$("#recnet_context_menu").hide();
	} );

	
}

function SetLoginPanelVisibility(visible) {
  var leftBoard = document.getElementById("LeftBoard");
  leftBoard.style.display = "block";
}

function HandleStudio( pVal )
{
	let strCmd = pVal['command'];
	
	if (strCmd == "get_recent_projects") {
    ShowRecentFileList(pVal["response"]);
  } else if (strCmd == "gallery_results") {
    OnGalleryResults(pVal);
  } else if (strCmd == "gallery_loaded") {
    OnGalleryLoaded(pVal);
  } else if (strCmd == "infinium_userlogin") {
    SetOrcaLoginInfo(pVal["data"]["avatar"], pVal["data"]["name"], pVal["data"]["account"]);
  } else if (strCmd == "infinium_useroffline") {
    SetOrcaUserOffline();
  } else if (strCmd == "studio_bambu_userlogin") {
    SetBambuLoginInfo(pVal["data"]["avatar"], pVal["data"]["name"]);
  } else if (strCmd == "studio_bambu_useroffline") {
    SetBambuUserOffline();
  } else if (strCmd == "studio_set_mallurl") {
    SetMallUrl(pVal["data"]["url"]);
  } else if (strCmd == "studio_clickmenu") {
    let strName = pVal["data"]["menu"];

    GotoMenu(strName);
  } else if (strCmd == "cloud_providers_info") {
    var providers = (pVal["data"] && pVal["data"]["providers"]) || [];

    if (providers.indexOf("bbl") >= 0) {
      $("#BambuCloudSection").show();
    } else {
      SetBambuUserOffline();
      $("#BambuCloudSection").hide();
    }

    if (providers.indexOf("orca") >= 0) {
      $("#LeftBoard").show();
    } else {
      $("#LeftBoard").hide();
    }
  } else if (strCmd == "network_plugin_installtip") {
    // Bambu Cloud is unreachable without the network plugin, so its row only carries the tip.
    let bMissing = pVal["show"] * 1 == 1;

    $("#NoPluginTip").css("display", bMissing ? "block" : "none");
    $("#BambuAccount").toggleClass("Disabled", bMissing).attr("aria-disabled", bMissing ? "true" : null);
    if (bMissing) SetBambuSectionExpanded(true, false);
  } else if (strCmd == "modelmall_model_advise_get") {
    //alert('hot');
    if (m_HotModelList != null) {
      let SS1 = JSON.stringify(pVal["hits"]);
      let SS2 = JSON.stringify(m_HotModelList);

      if (SS1 == SS2) return;
    }

    m_HotModelList = pVal["hits"];
    ShowStaffPick(m_HotModelList);
  } else if (strCmd == "SetLoginPanelVisibility") {
    SetLoginPanelVisibility(pVal["data"]["visible"]);
  }
}

function GotoMenu( strMenu )
{
	let MenuList=$(".BtnItem");
	let nAll=MenuList.length;
	
	for(let n=0;n<nAll;n++)
	{
		let OneBtn=MenuList[n];
		
		if( $(OneBtn).attr("menu")==strMenu )
		{
			$(".BtnItem").removeClass("BtnItemSelected");			
			
			$(OneBtn).addClass("BtnItemSelected");
			
			$("div[board]").hide();
			$("div[board=\'"+strMenu+"\']").show();
		}
	}
}

/*------Account rows------*/

/*----Everything that differs between the two cloud providers lives here----*/
var AccountRows = {
  orca:  { row: "#OrcaAccount",  name: "#UserName",      avatar: "#UserAvatarIcon",  dot: null,
           loginCmd: "homepage_infinium_login_or_register",  logoutCmd: "homepage_infinium_logout" },
  bambu: { row: "#BambuAccount", name: "#BambuUserName", avatar: "#BambuAvatarIcon", dot: "#BambuStatusDot",
           loginCmd: "homepage_bambu_login_or_register", logoutCmd: "homepage_bambu_logout" }
};

var BAMBU_FOLD_KEY = "OrcaHome_BambuCloudExpanded";

var m_OpenAccountMenu = null;

function SetAccountAvatar(selector, strAvatar) {
  if (strAvatar != null && strAvatar.trim() !== '') {
    if ($(selector).attr("src") !== strAvatar) $(selector).attr("src", strAvatar);
  } else {
    $(selector).removeAttr("src");
  }
}

function OnAvatarLoadError(img) {
  img.removeAttribute("src");  // fall back to the placeholder glyph
}

function SetAccountSignedIn(strProvider, strAvatar, strName, strHandle) {
  var account = AccountRows[strProvider];

  $(account.name).text(strName).attr("title", strName);  /*----long names are ellipsized----*/
  SetAccountAvatar(account.avatar, strAvatar);
  /*----the handle is the one thing the row does not already show----*/
  $(account.row).addClass("SignedIn").data("handle", strHandle !== strName ? strHandle : null);
  $(account.dot).addClass("Online");

  if (m_OpenAccountMenu === strProvider) OpenAccountMenu(strProvider);
}

function SetAccountSignedOut(strProvider) {
  var account = AccountRows[strProvider];

  if (m_OpenAccountMenu === strProvider) CloseAccountMenu();
  $(account.name).text('').removeAttr("title");
  SetAccountAvatar(account.avatar, null);
  $(account.row).removeClass("SignedIn").removeData("handle");
  $(account.dot).removeClass("Online");
}

function OnAccountRowClick(strProvider) {
  var account = AccountRows[strProvider];
  var row = $(account.row);
  if (row.hasClass("Disabled")) return;

  if (!row.hasClass("SignedIn")) {
    CloseAccountMenu();
    SendSimpleCommand(account.loginCmd);
  } else if (m_OpenAccountMenu === strProvider) {
    CloseAccountMenu();
  } else {
    OpenAccountMenu(strProvider);
  }
}

function OpenAccountMenu(strProvider) {
  var account = AccountRows[strProvider];
  var row = $(account.row);
  var top = row[0].offsetTop + row[0].offsetHeight + 6;  /*----read the geometry before writing----*/
  var strHandle = row.data("handle") || "";

  $("#AccountMenuHandle").text(strHandle).css("display", strHandle === "" ? "none" : "block");
  $("#AccountMenu").css("top", top + "px").addClass("Open");
  $(".AccountRow").removeClass("MenuOpen").attr("aria-expanded", "false");
  row.addClass("MenuOpen").attr("aria-expanded", "true");
  m_OpenAccountMenu = strProvider;
}

function CloseAccountMenu() {
  $("#AccountMenu").removeClass("Open");
  $(".AccountRow").removeClass("MenuOpen").attr("aria-expanded", "false");
  m_OpenAccountMenu = null;
}

function OnAccountMenuLogout() {
  var account = AccountRows[m_OpenAccountMenu];
  CloseAccountMenu();
  if (account) SendSimpleCommand(account.logoutCmd);
}

/*----Bambu Cloud is secondary, so its account folds away under the main one----*/
function SetBambuSectionExpanded(bExpanded, bPersist) {
  $("#BambuCloudBody").toggleClass("Expanded", bExpanded);
  $("#BambuAccount").attr("tabindex", bExpanded ? "0" : "-1");  /*----keep the folded row out of the tab order----*/
  $("#BambuCloudHeader").toggleClass("Expanded", bExpanded).attr("aria-expanded", bExpanded ? "true" : "false");
  if (!bExpanded && m_OpenAccountMenu === "bambu") CloseAccountMenu();
  // Best effort: the fold is a per-machine convenience, not a synced preference.
  if (bPersist) { try { localStorage.setItem(BAMBU_FOLD_KEY, bExpanded ? "1" : "0"); } catch (e) {} }
}

function ToggleBambuSection() {
  SetBambuSectionExpanded(!$("#BambuCloudBody").hasClass("Expanded"), true);
}

function Set_AccountMenu_Event() {
  var bExpanded = false;
  try { bExpanded = localStorage.getItem(BAMBU_FOLD_KEY) === "1"; } catch (e) {}
  SetBambuSectionExpanded(bExpanded, false);

  $(document).mousedown(function (e) {
    if (m_OpenAccountMenu === null) return;
    if ($(e.target).closest("#AccountMenu, .AccountRow").length === 0) CloseAccountMenu();
  });

  $(document).keydown(function (e) {
    if (m_OpenAccountMenu !== null && e.key === "Escape") CloseAccountMenu();
  });

  /*----Enter and Space activate the div-based buttons of this panel----*/
  $(document).on("keydown", "#LoginArea [role=button], #LoginArea [role=menuitem]", function (e) {
    if (e.key !== "Enter" && e.key !== " " && e.key !== "Spacebar") return;
    e.preventDefault();
    this.click();
  });
}

function SetMallUrl( strUrl )
{
	$("#MallWeb").prop("src",strUrl);
}


function ShowRecentFileList( pList )
{
	let nTotal=pList.length;
	
	let strHtml='';
	for(let n=0;n<nTotal;n++)
	{
		let OneFile=pList[n];
		
		let sPath=OneFile['path'];
		let sImg=OneFile["image"] || sImages[sPath];
		let sTime=OneFile['time'];
		let sName=OneFile['project_name'];
		let sPublished=OneFile['published'] == '1';
		sImages[sPath] = sImg;
		
		//let index=sPath.lastIndexOf('\\')>0?sPath.lastIndexOf('\\'):sPath.lastIndexOf('\/');
		//let sShortName=sPath.substring(index+1,sPath.length);
		
		let sBadge=sPublished? '<span class="FilePublishedBadge">PUB</span>':'';
		let sLogoBadge=sPublished? '<img class="FileLogoBadge" src="../../images/Infinium_gradient_circle.svg" alt="" />':'';

		let TmpHtml='<div class="FileItem"  fpath="'+sPath+'"  >'+
				'<a class="FileTip" title="'+sPath+'"></a>'+
				'<div class="FileImg" ><img src="'+sImg+'" onerror="this.onerror=null;this.src=\'img/d.png\';"  alt="No Image"  />'+sLogoBadge+'</div>'+
				'<div class="FileNamePack">'+sBadge+'<div class="FileName TextS1">'+sName+'</div></div>'+
				'<div class="FileDate">'+sTime+'</div>'+
			    '</div>';
		
		strHtml+=TmpHtml;
	}
	
	$("#FileList").html(strHtml);	
	
    Set_RecentFile_MouseRightBtn_Event();
	UpdateRecentClearBtnDisplay();
}

function ShowRecnetFileContextMenu()
{
	$("#recnet_context_menu").offset({top: 10000, left:-10000});
	$('#recnet_context_menu').show();
	
	let ContextMenuWidth=$('#recnet_context_menu').width();
	let ContextMenuHeight=$('#recnet_context_menu').height();
	
    let DocumentWidth=$(document).width();
	let DocumentHeight=$(document).height();

	let RealX=MousePosX;
	let RealY=MousePosY;
	
	if( MousePosX + ContextMenuWidth + 24 >DocumentWidth )
		RealX=DocumentWidth-ContextMenuWidth-24;
	if( MousePosY+ContextMenuHeight+24>DocumentHeight )
		RealY=DocumentHeight-ContextMenuHeight-24;
	
	$("#recnet_context_menu").offset({top: RealY, left:RealX});
}

/*-------RecentFile MX Message------*/
function SendMsg_GetLoginInfo()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="get_login_info";
	
	SendWXMessage( JSON.stringify(tSend) );	
}

function SendSimpleCommand(command) {
  var tSend = {};
  tSend['sequence_id'] = Math.round(new Date() / 1000);
  tSend['command'] = command;
  SendWXMessage(JSON.stringify(tSend));
}

function SendMsg_GetOrcaLoginInfo() { SendSimpleCommand("get_infinium_login_info"); }


function SendMsg_GetRecentFile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="get_recent_projects";
	
	SendWXMessage( JSON.stringify(tSend) );
}


function OnClickModelDepot()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_modeldepot";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

// INFINIUM: start-screen tiles
// INFINIUM: open the Marketplace tab (printer makers' apps, free model sites, help and firmware).
function OnClickMarketplace()
{
	SendSimpleCommand("homepage_open_marketplace");
}

function OnClickImportModel()
{
	SendSimpleCommand("homepage_import_model");
}

function OnClickFunModel(strModel)
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_fun_model";
	tSend['data']={'model': strModel};

	SendWXMessage( JSON.stringify(tSend) );
}

function OnClickNewProject()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_newproject";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function OnClickOpenProject()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_openproject";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function OnOpenRecentFile( strPath )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_open_recentfile";
	tSend['data']={};
	tSend['data']['path']=decodeURI(strPath);
	
	SendWXMessage( JSON.stringify(tSend) );	
}

function OnDeleteRecentFile( )
{
	//Clear in UI
	$("#recnet_context_menu").hide();
	
	let AllFile=$(".FileItem");
	let nFile=AllFile.length;
	for(let p=0;p<nFile;p++)
	{
		let pp=AllFile[p].getAttribute("fpath");
		if(pp==RightBtnFilePath)
			$(AllFile[p]).remove();
	}	
	
	UpdateRecentClearBtnDisplay();
	
	//Send Msg to C++
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_delete_recentfile";
	tSend['data']={};
	tSend['data']['path']=RightBtnFilePath;
	
	SendWXMessage( JSON.stringify(tSend) );
}

function OnDeleteAllRecentFiles()
{
	$('#FileList').html('');
	UpdateRecentClearBtnDisplay();
	
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_delete_all_recentfile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function UpdateRecentClearBtnDisplay()
{
    let AllFile=$(".FileItem");
	let nFile=AllFile.length;	
	if( nFile>0 )
		$("#RecentClearAllBtn").show();
	else
		$("#RecentClearAllBtn").hide();
}




function OnExploreRecentFile( )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="homepage_explore_recentfile";
	tSend['data']={};
	tSend['data']['path']=decodeURI(RightBtnFilePath);
	
	SendWXMessage( JSON.stringify(tSend) );	
	
	$("#recnet_context_menu").hide();
}

// --- Cloud providers ---

function SetOrcaLoginInfo(strAvatar, strName, strAccount) { SetAccountSignedIn("orca", strAvatar, strName, strAccount); }
function SetOrcaUserOffline() { SetAccountSignedOut("orca"); }
function SetBambuLoginInfo(strAvatar, strName) { SetAccountSignedIn("bambu", strAvatar, strName, null); }
function SetBambuUserOffline() { SetAccountSignedOut("bambu"); }

function SendMsg_GetBambuLoginInfo() { SendSimpleCommand("get_bambu_login_info"); }

function BeginDownloadNetworkPlugin()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="begin_network_plugin_download";
	
	SendWXMessage( JSON.stringify(tSend) );		
}

function OutputKey(keyCode, isCtrlDown, isShiftDown, isCmdDown) {
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "get_web_shortcut";
	tSend['key_event'] = {};
	tSend['key_event']['key'] = keyCode;
	tSend['key_event']['ctrl'] = isCtrlDown;
	tSend['key_event']['shift'] = isShiftDown;
	tSend['key_event']['cmd'] = isCmdDown;

	SendWXMessage(JSON.stringify(tSend));
}

//-------------User Manual------------

function OpenWikiUrl( strUrl )
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="userguide_wiki_open";
	tSend['data']={};
	tSend['data']['url']=strUrl;
	
	SendWXMessage( JSON.stringify(tSend) );	
}

//--------------Staff Pick-------
var StaffPickSwiper=null;
function InitStaffPick()
{
	if( StaffPickSwiper!=null )
	{
		StaffPickSwiper.destroy(true,true);
		StaffPickSwiper=null;
	}	
	
	StaffPickSwiper = new Swiper('#HotModel_Swiper.swiper', {
            slidesPerView : 'auto',
		    spaceBetween: 16,
			navigation: {
				nextEl: '.swiper-button-next',
				prevEl: '.swiper-button-prev',
			},
		    slidesPerView : 'auto',
		    slidesPerGroup : 3

			});
}

function SendMsg_GetStaffPick()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="modelmall_model_advise_get";
	
	SendWXMessage( JSON.stringify(tSend) );
	
	setTimeout("SendMsg_GetStaffPick()",3600*1000*1);
}

function ShowStaffPick( ModelList )
{
	let PickTotal=ModelList.length;
	if(PickTotal==0)
	{
		$('#HotModelList').html('');
		$('#HotModelArea').hide();
		
		return;
	}
	
	let strPickHtml='';
	for(let a=0;a<PickTotal;a++)
	{
		let OnePickModel=ModelList[a];
		
		let ModelID=OnePickModel['design']['id'];
		let ModelName=OnePickModel['design']['title'];
		let ModelCover=OnePickModel['design']['cover']+'?image_process=resize,w_200/format,webp';
		
		let DesignerName=OnePickModel['design']['designCreator']['name'];
		let DesignerAvatar=OnePickModel['design']['designCreator']['avatar']+'?image_process=resize,w_32/format,webp';
		
		strPickHtml+='<div class="HotModelPiece swiper-slide"  onClick="OpenOneStaffPickModel('+ModelID+')" >'+
			    '<div class="HotModel_Designer_Info"><img src="'+DesignerAvatar+'" /><span class="TextS2">'+DesignerName+'</span></div>'+
				'	<div class="HotModel_PrevBlock"><img class="HotModel_PrevImg" src="'+ModelCover+'" /></div>'+
				'	<div  class="HotModel_NameText TextS1" title="'+ModelName+'">'+ModelName+'</div>'+
				'</div>';
	}
	
	$('#HotModelList').html(strPickHtml);
	InitStaffPick();
	$('#HotModelArea').show();
}

function OpenOneStaffPickModel( ModelID )
{
	//alert(ModelID);
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="modelmall_model_open";
	tSend['data']={};
	tSend['data']['id']=ModelID;
	
	SendWXMessage( JSON.stringify(tSend) );		
}


//---------------Global-----------------
window.postMessage = HandleStudio;



/*------INFINIUM: model gallery------*/
// The fun tiles open a gallery: our own model first (works without internet), then many more from
// Printables.com. The app does the searching and downloading; names are only ever set as text.
var GalleryTopics = {
  cat:   { title: "Pick your cat!",   query: "cat",         builtin: "cat",   emoji: "🐱", name: "Our cat" },
  boat:  { title: "Pick your boat!",  query: "boat toy",    builtin: "boat",  emoji: "⛵", name: "Our boat" },
  bunny: { title: "Pick your bunny!", query: "bunny",       builtin: "bunny", emoji: "🐰", name: "Our bunny" },
  cube:  { title: "Pick your cube!",  query: "fidget cube", builtin: "cube",  emoji: "🧊", name: "Our cube" }
};

var m_Gallery = { query: "", offset: 0, topic: null, busy: false, loading: null };

function ShowBoard(strBoard)
{
	$("div[board]").hide();
	$("div[board='" + strBoard + "']").show();
}

function OpenGallery(strTopic)
{
	let topic = GalleryTopics[strTopic] || null;
	StartGallery(topic ? topic.query : strTopic, topic);
}

function StartGallery(strQuery, topic)
{
	m_Gallery = { query: strQuery, offset: 0, topic: topic, busy: false, loading: null };

	$("#GalleryTitle").text(topic ? topic.title : ("“" + strQuery + "”"));
	$("#GallerySub").text("Tap one to put it on your plate.");
	$("#GallerySearchInput").val(topic ? "" : strQuery).attr("placeholder", "Find something else...");
	$("#GalleryList").empty();
	$("#GalleryMore").hide();

	if (topic) {
		let card = $("<div class='GalleryItem GalleryBuiltin' tabindex='0' role='button'></div>");
		card.append($("<div class='GalleryImg GalleryEmoji'></div>").text(topic.emoji));
		card.append($("<div class='GalleryName TextS1'></div>").text(topic.name));
		card.append($("<div class='GalleryMaker'></div>").text("Ready now, no internet needed"));
		card.on("click", function() { OnClickFunModel(topic.builtin); });
		$("#GalleryList").append(card);
	}

	ShowBoard("gallery");
	$("#GalleryBoard").scrollTop(0);
	RequestGalleryPage();
}

function RequestGalleryPage()
{
	if (m_Gallery.busy) return;
	m_Gallery.busy = true;
	$("#GalleryMore").hide();
	$("#GalleryStatus").removeClass("GalleryError").html("<span class='Spinner'></span>").append($("<span></span>").text(" Looking for models..."));

	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "homepage_gallery_search";
	tSend['data'] = { 'query': m_Gallery.query, 'offset': m_Gallery.offset };
	SendWXMessage(JSON.stringify(tSend));
}

function GalleryLoadMore()
{
	RequestGalleryPage();
}

function OnGalleryResults(pVal)
{
	if (pVal["ok"] === false) {
		m_Gallery.busy = false;
		$("#GalleryStatus").addClass("GalleryError").text("We can't reach the internet right now. " +
			(m_Gallery.topic ? "You can still use our model!" : "Try again later."));
		return;
	}
	if (pVal["query"] !== m_Gallery.query || pVal["offset"] !== m_Gallery.offset) return;  // an old search
	m_Gallery.busy = false;

	let items = pVal["items"] || [];
	for (let n = 0; n < items.length; n++) {
		let it = items[n];
		let card = $("<div class='GalleryItem' tabindex='0' role='button'></div>").attr("data-id", it.id);
		let img = $("<img class='GalleryImg' loading='lazy' alt='' />").attr("src", it.image);
		img.on("error", function() {
			if (this.src !== it.image_full) this.src = it.image_full; else $(this).addClass("GalleryImgMissing");
		});
		card.append(img);
		card.append($("<div class='GalleryName TextS1'></div>").text(it.name).attr("title", it.name));
		card.append($("<div class='GalleryMaker'></div>").text(it.maker ? ("by " + it.maker) : ""));
		card.attr("title", it.license ? (it.name + "\nby " + it.maker + "\n" + it.license) : it.name);
		card.on("click", function() { OnGalleryPick(it.id, card); });
		$("#GalleryList").append(card);
	}

	m_Gallery.offset += 24;
	let shown = $("#GalleryList .GalleryItem").not(".GalleryBuiltin").length;
	if (shown === 0 && !pVal["more"])
		$("#GalleryStatus").text("Nothing found. Try another word!");
	else
		$("#GalleryStatus").empty();
	if (pVal["more"]) $("#GalleryMore").show();
}

function OnGalleryPick(strId, card)
{
	if (m_Gallery.loading) return;  // one at a time
	m_Gallery.loading = strId;
	$(".GalleryItem").removeClass("GalleryPicked");
	card.addClass("GalleryPicked GalleryLoading");
	card.append($("<div class='GalleryOverlay'><span class='Spinner'></span><span>Getting it...</span></div>"));

	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "homepage_gallery_load";
	tSend['data'] = { 'id': strId };
	SendWXMessage(JSON.stringify(tSend));
}

function OnGalleryLoaded(pVal)
{
	let card = $(".GalleryItem[data-id='" + m_Gallery.loading + "']");
	card.removeClass("GalleryLoading").find(".GalleryOverlay").remove();
	m_Gallery.loading = null;
	if (pVal["ok"] === false) {
		card.append($("<div class='GalleryOverlay GalleryOverlayError'></div>").text("Sorry, this one won't load. Pick another!"));
		setTimeout(function() { card.find(".GalleryOverlay").fadeOut(400, function() { $(this).remove(); }); }, 2500);
	}
}

function CloseGallery()
{
	ShowBoard("recent");
}

function OnGallerySearch(e)
{
	e.preventDefault();
	let q = $.trim($("#GallerySearchInput").val());
	if (q.length > 0) StartGallery(q, null);
}

function OnHomeSearch(e)
{
	e.preventDefault();
	let q = $.trim($("#HomeSearchInput").val());
	if (q.length > 0) StartGallery(q, null);
}
