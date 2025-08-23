//
//  Item.swift
//  zh-sanfang
//
//  Created by 公子语 on 2025/8/22.
//

import Foundation
import SwiftData

@Model
final class Item {
    var timestamp: Date
    
    init(timestamp: Date) {
        self.timestamp = timestamp
    }
}
