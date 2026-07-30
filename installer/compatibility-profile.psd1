@{
    SchemaVersion = 1
    RecommendedVersion = '1.10.3+68-42'
    RecommendedSource = 'Steam Enhanced Edition, AppID 2427420'
    KnownSha256 = @(
        '89BA7FC6B84BB18A3D0B47936B2E67BD1B7CC8B642A4F322B068C9774A8741E1'
    )
    Signatures = @(
        @{
            Name = 'UICore::OnDeviceReset'
            ExpectedRva = 0xF2A80
            Hex = '48 83 EC 38 8B 05 7E 04 C5 00 48 8D 54 24 20 C5 F8 57 C0 C4'
        }
        @{
            Name = 'UICore::UICore'
            ExpectedRva = 0xF2120
            Hex = '48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8D 05 DA 12'
        }
        @{
            Name = 'dxUIRender::SetScissor'
            ExpectedRva = 0x743040
            Hex = '40 53 48 83 EC 20 48 8B 0D AB 80 64 00 48 8B DA E8 5B 3F FA'
        }
        @{
            Name = 'dxUIRender::PushPoint'
            ExpectedRva = 0x743120
            Hex = '4C 8B C1 8B 49 1C 85 C9 74 3A 83 F9 01 75 72 49 8B 50 40 C5'
        }
        @{
            Name = 'dxUIRender::StartPrimitive'
            ExpectedRva = 0x7431B0
            Hex = '40 53 48 83 EC 20 89 51 20 48 8B D9 44 89 41 18 44 89 49 1C'
        }
        @{
            Name = 'dxUIRender::FlushPrimitive'
            ExpectedRva = 0x743230
            Hex = '40 53 48 83 EC 20 83 79 1C 00 48 8B D9 48 89 74 24 30 48 89'
        }
        @{
            Name = 'dxFontRender::OnRender'
            ExpectedRva = 0x7AF7F0
            Hex = '48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 45 33 C9 C6 41'
        }
        @{
            Name = 'CUICursor::UpdateCursorPosition'
            ExpectedRva = 0x120060
            Hex = '40 53 48 83 EC 30 48 8B D9 E8 32 36 F2 FF 83 78 20 02 0F 84'
        }
        @{
            Name = 'CWeapon::render_item_ui'
            ExpectedRva = 0x113A20
            Hex = '48 89 5C 24 08 57 48 83 EC 20 48 8B 01 48 8B D9 FF 90 88 02'
        }
        @{
            Name = 'CUICustomMap::Draw'
            ExpectedRva = 0x292080
            Hex = 'E9 BB 50 F4 FF'
        }
        @{
            Name = 'input mode reference'
            ExpectedRva = 0x436B0
            Hex = '83 3D 79 FA C9 00 02 0F 94 C0 C3'
        }
    )
}
