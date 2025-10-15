function init()
  local files
  local loaded_files = {}
  local layout = g_resources:getLayout()
  local settings = g_configs.getSettings()
  local autoImportTTF = true
  if settings and settings:exists('autoImportTTF') then
    local v = settings:getValue('autoImportTTF')
    autoImportTTF = (v == true) or (type(v) == 'string' and v:lower() == 'true')
  end

  -- 1) Import fonts first (layout and data), so styles that reference fonts don't fail
  if layout:len() > 0 then
    files = g_resources.listDirectoryFiles('/layouts/' .. layout .. '/fonts')
    for _,file in pairs(files) do
      if g_resources.isFileType(file, 'otfont') then
        g_fonts.importFont('/layouts/' .. layout .. '/fonts/' .. file)
        loaded_files[file] = true
      end
    end
  end

  files = g_resources.listDirectoryFiles('/data/fonts')
  for _,file in pairs(files) do
    if g_resources.isFileType(file, 'otfont') and not loaded_files[file] then
      g_fonts.importFont('/data/fonts/' .. file)
    end
  end

  -- Optional auto-import of TTF/OTF files
  if autoImportTTF then
    local function importDirFonts(dir, prefix)
      local list = g_resources.listDirectoryFiles(dir)
      for _,file in pairs(list) do
        -- Case-insensitive detection for .ttf/.otf extensions
        local lower = file:lower()
        if lower:match('%.ttf$') or lower:match('%.otf$') then
          -- Strip extension case-insensitively to form the font name
          local name = file:gsub('%.[Tt][Tt][Ff]$', ''):gsub('%.[Oo][Tt][Ff]$', '')
          if not g_fonts.fontExists(name) then
            local path = dir .. '/' .. file
            g_logger.info('Importando fonte TTF/OTF: ' .. name .. ' (' .. path .. ')')
            g_fonts.importTTFFont(path, name, 18, 0)
          end
        end
      end
    end
    -- standard fonts location
    importDirFonts('/data/fonts')
    -- also scan root (useful for bundled TTF like 'Pokemon Solid.ttf')
    importDirFonts('/')
    -- explicit import for common bundled font if present in project root
    if g_resources.fileExists('Pokemon Solid.ttf') and not g_fonts.fontExists('Pokemon Solid') then
      g_fonts.importTTFFont('Pokemon Solid.ttf', 'Pokemon Solid', 18, 0)
    end
  end

  -- 2) Then import styles from layout and data
  local style_files = {}
  if layout:len() > 0 then
    files = g_resources.listDirectoryFiles('/layouts/' .. layout .. '/styles')
    for _,file in pairs(files) do
      if g_resources.isFileType(file, 'otui') then
        table.insert(style_files, file)
        loaded_files[file] = true
      end
    end  
  end

  files = g_resources.listDirectoryFiles('/data/styles')
  for _,file in pairs(files) do
    if g_resources.isFileType(file, 'otui') and not loaded_files[file] then
        table.insert(style_files, file)
    end
  end

  table.sort(style_files)
  for _,file in pairs(style_files) do
    if g_resources.isFileType(file, 'otui') then
      g_ui.importStyle('/styles/' .. file)
    end
  end

  g_mouse.loadCursors('/data/cursors/cursors')
  if layout:len() > 0 and g_resources.directoryExists('/layouts/' .. layout .. '/cursors/cursors') then
    g_mouse.loadCursors('/layouts/' .. layout .. '/cursors/cursors')    
  end
end

function terminate()
end

