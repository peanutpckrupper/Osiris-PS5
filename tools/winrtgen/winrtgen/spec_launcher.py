import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from gen import emit_statics
H='windows.system.h'
no = '{ CompatNote("Launcher: nothing is launched offline"); return Make<DoneValue<bool>>(false).CopyTo(operation); }'
print(emit_statics('LauncherStatics', [('ABI::Windows::System::ILauncherStatics', H), ('ABI::Windows::System::ILauncherStatics2', H), ('ABI::Windows::System::ILauncherStatics3', H), ('ABI::Windows::System::ILauncherStatics4', H)], bodies={
 'LaunchFileAsync': no, 'LaunchFileWithOptionsAsync': no,
 'LaunchUriAsync': '{ LogUri("Launcher.LaunchUriAsync", uri); return Make<DoneValue<bool>>(false).CopyTo(operation); }',
 'QueryUriSupportAsync': '{ LogUri("Launcher.QueryUriSupportAsync", uri); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }',
 'QueryUriSupportWithPackageFamilyNameAsync': '{ LogUri("Launcher.QueryUriSupportWithPackageFamilyNameAsync", uri); LogText("  package", packageFamilyName); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }',
 'QueryAppUriSupportAsync': '{ LogUri("Launcher.QueryAppUriSupportAsync", uri); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }',
 'LaunchUriWithOptionsAsync': '{ LogUri("Launcher.LaunchUriWithOptionsAsync", uri); return Make<DoneValue<bool>>(false).CopyTo(operation); }'}))
from gen import emit
print(emit('LauncherOptions','ABI::Windows::System::ILauncherOptions',H,'Windows.System.LauncherOptions'))
