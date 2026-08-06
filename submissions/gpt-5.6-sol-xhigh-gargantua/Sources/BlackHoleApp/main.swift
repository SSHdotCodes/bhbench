import AppKit

let application = NSApplication.shared
application.setActivationPolicy(.regular)

let mainMenu = NSMenu()
let appMenuItem = NSMenuItem()
mainMenu.addItem(appMenuItem)
let appMenu = NSMenu()
appMenu.addItem(withTitle: "Quit Gargantua", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
appMenuItem.submenu = appMenu
application.mainMenu = mainMenu

let delegate = AppDelegate(options: AppOptions())
application.delegate = delegate
application.run()
